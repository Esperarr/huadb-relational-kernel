#include <algorithm>
#include <climits>
#include <limits>

#include "common/exceptions.h"
#include "common/string_util.h"
#include "operators/expressions/column_value.h"
#include "operators/expressions/comparison.h"
#include "operators/expressions/const.h"
#include "operators/expressions/logic.h"
#include "operators/nested_loop_join_operator.h"
#include "operators/seqscan_operator.h"
#include "optimizer/optimizer.h"
#include "planner/planner.h"

namespace huadb {

// 你可以定义一些辅助函数
// LAB 5 BEGIN

// 辅助：判断表达式是否是常量 true
static bool JR_IsConstTrue(const std::shared_ptr<OperatorExpression> &expr) {
  if (!expr) return false;
  if (expr->GetExprType() != OperatorExpressionType::CONST) return false;
  auto c = std::dynamic_pointer_cast<Const>(expr);
  if (c->value_.IsNull()) return false;
  if (c->value_.GetType() != Type::BOOL) return false;
  return c->value_.GetValue<bool>();
}

// 辅助：收集子树中所有 SeqScan 的表名
static void JR_GetTablesCovered(const std::shared_ptr<Operator> &plan,
                                 std::set<std::string> &tables) {
  if (plan->GetType() == OperatorType::SEQSCAN) {
    auto scan = std::dynamic_pointer_cast<SeqScanOperator>(plan);
    tables.insert(scan->GetTableNameOrAlias());
    return;
  }
  for (const auto &child : plan->children_) {
    JR_GetTablesCovered(child, tables);
  }
}

// 辅助：收集表达式中所有引用的表名（列名格式 "table.col"）
static void JR_GetReferencedTables(const std::shared_ptr<OperatorExpression> &expr,
                                    std::set<std::string> &tables) {
  if (expr->GetExprType() == OperatorExpressionType::COLUMN_VALUE) {
    const auto &name = expr->name_;
    auto dot = name.find('.');
    if (dot != std::string::npos) {
      tables.insert(name.substr(0, dot));
    }
    return;
  }
  for (const auto &child : expr->children_) {
    JR_GetReferencedTables(child, tables);
  }
}

// 辅助：通用表达式重写（对 ColumnValue 应用 lambda，递归重建 Comparison/Logic）
static std::shared_ptr<OperatorExpression> JR_RewriteExpr(
    const std::shared_ptr<OperatorExpression> &expr,
    const std::function<std::shared_ptr<OperatorExpression>(
        std::shared_ptr<ColumnValue>)> &rewrite_cv) {
  if (expr->GetExprType() == OperatorExpressionType::COLUMN_VALUE) {
    return rewrite_cv(std::dynamic_pointer_cast<ColumnValue>(expr));
  }
  if (expr->GetExprType() == OperatorExpressionType::CONST) {
    return expr;
  }
  std::vector<std::shared_ptr<OperatorExpression>> new_children;
  new_children.reserve(expr->children_.size());
  for (const auto &child : expr->children_) {
    new_children.push_back(JR_RewriteExpr(child, rewrite_cv));
  }
  if (expr->GetExprType() == OperatorExpressionType::COMPARISON) {
    auto cmp = std::dynamic_pointer_cast<Comparison>(expr);
    return std::make_shared<Comparison>(cmp->GetComparisonType(),
                                        new_children[0], new_children[1]);
  }
  if (expr->GetExprType() == OperatorExpressionType::LOGIC) {
    auto logic = std::dynamic_pointer_cast<Logic>(expr);
    if (logic->GetLogicType() == LogicType::NOT) {
      return std::make_shared<Logic>(LogicType::NOT, new_children[0]);
    }
    return std::make_shared<Logic>(logic->GetLogicType(),
                                   new_children[0], new_children[1]);
  }
  return expr;
}

// 辅助：将谓词改写为以 left/right 为两侧时的 join condition
static std::shared_ptr<OperatorExpression> JR_RewriteForJoinCond(
    const std::shared_ptr<OperatorExpression> &expr,
    const std::shared_ptr<Operator> &left_child,
    const std::shared_ptr<Operator> &right_child) {
  return JR_RewriteExpr(expr, [&](std::shared_ptr<ColumnValue> cv)
      -> std::shared_ptr<OperatorExpression> {
    auto left_idx = left_child->OutputColumns().TryGetColumnIndex(cv->name_);
    if (left_idx) {
      const auto &col = left_child->OutputColumns().GetColumn(*left_idx);
      return std::make_shared<ColumnValue>(*left_idx, col.GetType(),
                                           cv->name_, col.GetMaxSize(), true);
    }
    auto right_idx = right_child->OutputColumns().TryGetColumnIndex(cv->name_);
    if (right_idx) {
      const auto &col = right_child->OutputColumns().GetColumn(*right_idx);
      return std::make_shared<ColumnValue>(*right_idx, col.GetType(),
                                           cv->name_, col.GetMaxSize(), false);
    }
    return cv;
  });
}

// 辅助：将 NestedLoopJoin 子树展平，收集叶节点（base plans）和连接条件
static void FlattenJoins(const std::shared_ptr<Operator> &plan,
                          std::vector<std::shared_ptr<Operator>> &base_plans,
                          std::vector<std::shared_ptr<OperatorExpression>> &predicates) {
  if (plan->GetType() == OperatorType::NESTEDLOOP) {
    auto join = std::dynamic_pointer_cast<NestedLoopJoinOperator>(plan);
    FlattenJoins(join->children_[0], base_plans, predicates);
    FlattenJoins(join->children_[1], base_plans, predicates);
    if (!JR_IsConstTrue(join->join_condition_)) {
      predicates.push_back(join->join_condition_);
    }
  } else {
    base_plans.push_back(plan);
  }
}

// 主函数：ReorderJoin（贪心算法）
std::shared_ptr<Operator> Optimizer::ReorderJoin(std::shared_ptr<Operator> plan) {
  // 通过 catalog_.GetCardinality 和 catalog_.GetDistinct 从系统表中读取表和列的元信息
  // 可根据 join_order_algorithm_ 变量的值选择不同的连接顺序选择算法，默认为 None，表示不进行连接顺序优化  
  if (join_order_algorithm_ == JoinOrderAlgorithm::NONE) {
    return plan;  // 默认配置下连扫描都省了
  }
  // LAB 5 BEGIN
  // 非 NestedLoopJoin 节点：只递归子树，不做 join 重排
  if (plan->GetType() != OperatorType::NESTEDLOOP) {
    for (auto &child : plan->children_) {
      child = ReorderJoin(child);
    }
    return plan;
  }
  // NestedLoopJoin：先完整展平整个连续 NLJ 子树，再一次性贪心。不在展平前对 NLJ 子节点递归，以免部分重排破坏叶节点的顺序。
  std::vector<std::shared_ptr<Operator>> base_plans;
  std::vector<std::shared_ptr<OperatorExpression>> all_predicates;
  FlattenJoins(plan, base_plans, all_predicates);

  // 对非 NLJ 叶节点（子查询等）单独递归处理
  for (auto &bp : base_plans) {
    bp = ReorderJoin(bp);
  }
  size_t n = base_plans.size();
  if (n <= 1) {
    return base_plans.empty() ? plan : base_plans[0];
  }
  // 贪心算法
  // 估算基数：单表基表用 catalog；多表子树粗略相乘
  std::vector<double> sizes(n);
  std::vector<std::set<std::string>> table_sets(n);
  for (size_t i = 0; i < n; i++) {
    JR_GetTablesCovered(base_plans[i], table_sets[i]);
    double card = 1.0;
    for (const auto &tname : table_sets[i]) {
      uint32_t c = catalog_.GetCardinality(tname);
      if (c != static_cast<uint32_t>(-1)) {
        card *= static_cast<double>(c);
      }
    }
    sizes[i] = card;
  }

  // 辅助 lambda：估算 left 子树（覆盖 left_tables，基数 left_card），与 base_plans[idx] 做 join 后的输出大小
  auto estimate_join = [&](double left_card,
                           const std::set<std::string> &left_tables,
                           size_t idx) -> std::pair<double, bool> {
    double cross   = left_card * sizes[idx];
    double sel_min = 1.0;
    bool   has_pred = false;
    for (const auto &pred : all_predicates) {
      std::set<std::string> ref_tables;
      JR_GetReferencedTables(pred, ref_tables);
      bool touches_left = false, touches_right = false;
      for (const auto &t : ref_tables) {
        if (left_tables.count(t))         touches_left  = true;
        if (table_sets[idx].count(t))     touches_right = true;
      }
      if (!touches_left || !touches_right) continue;
      has_pred = true;
      if (pred->GetExprType() == OperatorExpressionType::COMPARISON) {
        auto cmp = std::dynamic_pointer_cast<Comparison>(pred);
        if (cmp->GetComparisonType() == ComparisonType::EQUAL &&
            cmp->children_[0]->GetExprType() == OperatorExpressionType::COLUMN_VALUE &&
            cmp->children_[1]->GetExprType() == OperatorExpressionType::COLUMN_VALUE) {
          const auto &n1 = cmp->children_[0]->name_;
          const auto &n2 = cmp->children_[1]->name_;
          auto d1p = n1.find('.');
          auto d2p = n2.find('.');
          std::string t1 = n1.substr(0, d1p), c1 = n1.substr(d1p + 1);
          std::string t2 = n2.substr(0, d2p), c2 = n2.substr(d2p + 1);
          uint32_t dv1 = catalog_.GetDistinct(t1, c1);
          uint32_t dv2 = catalog_.GetDistinct(t2, c2);
          if (dv1 == static_cast<uint32_t>(-1)) dv1 = 1;
          if (dv2 == static_cast<uint32_t>(-1)) dv2 = 1;
          double sel = 1.0 / static_cast<double>(std::max(dv1, dv2));
          sel_min = std::min(sel_min, sel);
        }
      }
    }
    return {has_pred ? cross * sel_min : cross, has_pred};
  };
  // 选基数最小的单表作为起点 cur
  std::vector<bool> selected(n, false);
  // 每个基表关联的连接谓词数（用于平局破除）
  auto count_preds_touching = [&](size_t i) -> int {
    int cnt = 0;
    for (const auto &pred : all_predicates) {
      std::set<std::string> ref_tables;
      JR_GetReferencedTables(pred, ref_tables);
      bool touches = false;
      for (const auto &t : ref_tables) {
        if (table_sets[i].count(t)) { touches = true; break; }
      }
      if (touches) cnt++;
    }
    return cnt;
  };
  size_t start = 0;
  int    start_preds = count_preds_touching(0);
  for (size_t i = 1; i < n; i++) {
    int preds_i = count_preds_touching(i);
    if (sizes[i] < sizes[start] ||
        (sizes[i] == sizes[start] && preds_i > start_preds)) {
      start = i;
      start_preds = preds_i;
    }
  }
  selected[start] = true;
  std::shared_ptr<Operator> cur     = base_plans[start];
  std::set<std::string>     cur_tab = table_sets[start];
  double                    cur_card = sizes[start];
  // 逐个把基表加入 cur
  for (size_t step = 1; step < n; step++) {
    size_t best_idx        = SIZE_MAX;
    double best_card_pred  = std::numeric_limits<double>::max();  // 有谓词的最佳
    size_t fallback_idx    = SIZE_MAX;
    double fallback_card   = std::numeric_limits<double>::max();  // 无谓词时按基数最小
    for (size_t i = 0; i < n; i++) {
      if (selected[i]) continue;
      auto [est, has_pred] = estimate_join(cur_card, cur_tab, i);
      if (has_pred) {
        if (est < best_card_pred) {
          best_card_pred = est;
          best_idx       = i;
        }
      } else {
        if (sizes[i] < fallback_card) {
          fallback_card = sizes[i];
          fallback_idx  = i;
        }
      }
    }
    size_t pick;
    double new_card;
    if (best_idx != SIZE_MAX) {
      pick     = best_idx;
      new_card = best_card_pred;
    } else {
      pick     = fallback_idx;
      new_card = cur_card * sizes[pick];  // 笛卡尔积
    }
    selected[pick] = true;
    // 构造 NestedLoopJoin: left = cur（累积子树），right = base_plans[pick]
    std::shared_ptr<Operator> left_plan  = cur;
    std::shared_ptr<Operator> right_plan = base_plans[pick];
    // 找出连接两侧的谓词并改写为 join condition；多个谓词用 AND 合并
    std::shared_ptr<OperatorExpression> join_cond =
        std::make_shared<Const>(Value(true));
    for (const auto &pred : all_predicates) {
      std::set<std::string> ref_tables;
      JR_GetReferencedTables(pred, ref_tables);
      bool touches_left = false, touches_right = false;
      for (const auto &t : ref_tables) {
        if (cur_tab.count(t))              touches_left  = true;
        if (table_sets[pick].count(t))     touches_right = true;
      }
      if (touches_left && touches_right) {
        auto rewritten = JR_RewriteForJoinCond(pred, left_plan, right_plan);
        if (JR_IsConstTrue(join_cond)) {
          join_cond = rewritten;
        } else {
          join_cond = std::make_shared<Logic>(LogicType::AND, join_cond, rewritten);
        }
      }
    }
    // 合并列列表
    auto col_list = std::make_shared<ColumnList>();
    for (const auto &col : left_plan->OutputColumns().GetColumns()) {
      col_list->AddColumn(col);
    }
    for (const auto &col : right_plan->OutputColumns().GetColumns()) {
      col_list->AddColumn(col);
    }

    cur = std::make_shared<NestedLoopJoinOperator>(
        col_list, left_plan, right_plan, join_cond);
    cur_tab.insert(table_sets[pick].begin(), table_sets[pick].end());
    cur_card = new_card;
  }

  return cur;
}

}  // namespace huadb