#include "common/exceptions.h"
#include "common/string_util.h"
#include "operators/expressions/column_value.h"
#include "operators/expressions/comparison.h"
#include "operators/expressions/const.h"
#include "operators/expressions/logic.h"
#include "operators/filter_operator.h"
#include "operators/nested_loop_join_operator.h"
#include "operators/seqscan_operator.h"
#include "optimizer/optimizer.h"

namespace huadb {

// 你可以定义一些辅助函数
// LAB 5 BEGIN
// 辅助：判断表达式是否是常量 true
static bool IsConstTrue(const std::shared_ptr<OperatorExpression> &expr) {
  if (!expr) return false;
  if (expr->GetExprType() != OperatorExpressionType::CONST) return false;
  auto c = std::dynamic_pointer_cast<Const>(expr);
  if (c->value_.IsNull()) return false;
  if (c->value_.GetType() != Type::BOOL) return false;
  return c->value_.GetValue<bool>();
}

// 辅助：收集子树中所有 SeqScan 覆盖的表名
static void GetTablesCovered(const std::shared_ptr<Operator> &plan,
                              std::set<std::string> &tables) {
  if (plan->GetType() == OperatorType::SEQSCAN) {
    auto scan = std::dynamic_pointer_cast<SeqScanOperator>(plan);
    tables.insert(scan->GetTableNameOrAlias());
    return;
  }
  for (const auto &child : plan->children_) {
    GetTablesCovered(child, tables);
  }
}

// 辅助：收集谓词中所有引用的表名（列名格式 "table.col"）
static void GetReferencedTables(const std::shared_ptr<OperatorExpression> &expr,
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
    GetReferencedTables(child, tables);
  }
}

// 辅助：通用表达式重写（对 ColumnValue 应用 lambda，递归重建 Comparison/Logic）
static std::shared_ptr<OperatorExpression> RewriteExpr(
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
    new_children.push_back(RewriteExpr(child, rewrite_cv));
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

// 辅助：将谓词改写为在 child 之上作 Filter 的版本（通过列名查 col_idx）
static std::shared_ptr<OperatorExpression> RewriteForFilter(
    const std::shared_ptr<OperatorExpression> &expr,
    const std::shared_ptr<Operator> &child) {
  return RewriteExpr(expr, [&](std::shared_ptr<ColumnValue> cv)
      -> std::shared_ptr<OperatorExpression> {
    auto idx_opt = child->OutputColumns().TryGetColumnIndex(cv->name_);
    if (idx_opt) {
      const auto &col = child->OutputColumns().GetColumn(*idx_opt);
      return std::make_shared<ColumnValue>(*idx_opt, col.GetType(),
                                           cv->name_, col.GetMaxSize(), true);
    }
    return cv;
  });
}

// 辅助：将谓词改写为 NestedLoopJoin 连接条件（左列 is_left=true，右列 is_left=false）
static std::shared_ptr<OperatorExpression> RewriteForJoinCond(
    const std::shared_ptr<OperatorExpression> &expr,
    const std::shared_ptr<Operator> &left_child,
    const std::shared_ptr<Operator> &right_child) {
  return RewriteExpr(expr, [&](std::shared_ptr<ColumnValue> cv)
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

// 辅助：将新谓词 AND 到已有连接条件（若已有条件是 true 则直接替换）
static std::shared_ptr<OperatorExpression> AppendJoinCond(
    const std::shared_ptr<OperatorExpression> &existing,
    const std::shared_ptr<OperatorExpression> &new_pred) {
  if (IsConstTrue(existing)) return new_pred;
  return std::make_shared<Logic>(LogicType::AND, existing, new_pred);
}

// 核心递归：携带 pending（待下推谓词）向下传播
static std::shared_ptr<Operator> PushDown(
    std::shared_ptr<Operator> plan,
    std::vector<std::shared_ptr<OperatorExpression>> pending) {

  // FILTER：收谓词、删除本节点，继续向下
  if (plan->GetType() == OperatorType::FILTER) {
    auto filter = std::dynamic_pointer_cast<FilterOperator>(plan);
    pending.push_back(filter->predicate_);
    return PushDown(filter->children_[0], std::move(pending));
  }

  // SEQSCAN：把所有待下推谓词逐层叠在 scan 之上
  if (plan->GetType() == OperatorType::SEQSCAN) {
    std::shared_ptr<Operator> result = plan;
    for (const auto &pred : pending) {
      auto rewritten = RewriteForFilter(pred, result);
      auto col_list = std::make_shared<ColumnList>(result->OutputColumns());
      result = std::make_shared<FilterOperator>(col_list, result, rewritten);
    }
    return result;
  }

  // NESTEDLOOP：三分 pending，递归子树，再处理跨表谓词
  if (plan->GetType() == OperatorType::NESTEDLOOP) {
    auto join = std::dynamic_pointer_cast<NestedLoopJoinOperator>(plan);

    std::set<std::string> left_tables, right_tables;
    GetTablesCovered(join->children_[0], left_tables);
    GetTablesCovered(join->children_[1], right_tables);

    std::vector<std::shared_ptr<OperatorExpression>> left_preds, right_preds, join_preds;

    for (const auto &pred : pending) {
      std::set<std::string> ref_tables;
      GetReferencedTables(pred, ref_tables);

      bool all_left = true, all_right = true;
      for (const auto &t : ref_tables) {
        if (!left_tables.count(t))  all_left  = false;
        if (!right_tables.count(t)) all_right = false;
      }

      if (ref_tables.empty() || all_left) {
        left_preds.push_back(pred);
      } else if (all_right) {
        right_preds.push_back(pred);
      } else {
        join_preds.push_back(pred);
      }
    }

    // 先完成子树递归（join_preds 的列下标重写依赖子树的最终输出列）
    join->children_[0] = PushDown(join->children_[0], std::move(left_preds));
    join->children_[1] = PushDown(join->children_[1], std::move(right_preds));

    // 将跨表谓词改写为连接条件
    for (const auto &pred : join_preds) {
      auto rewritten = RewriteForJoinCond(pred, join->children_[0], join->children_[1]);
      join->join_condition_ = AppendJoinCond(join->join_condition_, rewritten);
    }

    return join;
  }

  // 其他单子树算子（Projection 等）：透传所有谓词
  if (!plan->children_.empty()) {
    plan->children_[0] = PushDown(plan->children_[0], std::move(pending));
  }
  return plan;
}

std::shared_ptr<Operator> Optimizer::PushDownPredicates(std::shared_ptr<Operator> plan) {
  // LAB 5 BEGIN
  return PushDown(plan, {});
}

}  // namespace huadb