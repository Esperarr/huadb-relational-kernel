#include <algorithm>
#include <cassert>
#include <map>
#include <optional>
#include <set>

#include "index/btree_index.h"
#include "operators/btree_scan_operator.h"
#include "operators/expressions/column_value.h"
#include "operators/expressions/comparison.h"
#include "operators/expressions/const.h"
#include "operators/expressions/logic.h"
#include "operators/filter_operator.h"
#include "operators/rid_union_operator.h"
#include "operators/seqscan_operator.h"
#include "optimizer/optimizer.h"

namespace huadb {

// 你可以定义一些辅助函数
// LAB 6 BEGIN

// 从 OR 树中收集所有析取项
static void CollectOrParts(const std::shared_ptr<OperatorExpression> &expr, std::vector<std::shared_ptr<OperatorExpression>> &parts) {
  if (expr->GetExprType() == OperatorExpressionType::LOGIC) {
    auto logic = std::dynamic_pointer_cast<Logic>(expr);
    if (logic->GetLogicType() == LogicType::OR) {
      CollectOrParts(logic->children_[0], parts);
      CollectOrParts(logic->children_[1], parts);
      return;
    }
  }
  parts.push_back(expr);
}
// 把一个 OR 分支内部的 AND 递归拍平成原子谓词列表
static void CollectAndParts(const std::shared_ptr<OperatorExpression> &expr,
                            std::vector<std::shared_ptr<OperatorExpression>> &parts) {
  if (expr->GetExprType() == OperatorExpressionType::LOGIC) {
    auto logic = std::dynamic_pointer_cast<Logic>(expr);
    if (logic->GetLogicType() == LogicType::AND) {
      CollectAndParts(logic->children_[0], parts);
      CollectAndParts(logic->children_[1], parts);
      return;
    }
  }
  parts.push_back(expr);
}
// 将谓词列表（每项之间为 AND）展开为 DNF，每个谓词本身可能是 OR 表达式
static std::vector<std::vector<std::shared_ptr<OperatorExpression>>> ExpandToDNF(
    const std::vector<std::shared_ptr<OperatorExpression>> &preds) {
  std::vector<std::vector<std::shared_ptr<OperatorExpression>>> result = {{}};
  for (const auto &pred : preds) {
    std::vector<std::shared_ptr<OperatorExpression>> or_parts;
    CollectOrParts(pred, or_parts);
    std::vector<std::vector<std::shared_ptr<OperatorExpression>>> new_result;
    for (const auto &term : result) {
      for (const auto &part : or_parts) {
        auto new_term = term;
        // 关键修复：把这个 OR 分支内部的 AND 也拍平成原子谓词，
        // 否则 (a=1 AND b=2) 整块会被当成一个谓词，AsColumnOpConst 认不出来，无法命中索引。
        std::vector<std::shared_ptr<OperatorExpression>> and_parts;
        CollectAndParts(part, and_parts);
        for (auto &ap : and_parts) {
          new_term.push_back(ap);
        }
        new_result.push_back(new_term);
      }
    }
    result = std::move(new_result);
  }
  return result;
}
// 尝试将谓词匹配为 (列 op 常量) 的形式，其中 op 是可索引的
struct AtomicPredInfo {
  size_t col_idx;// 列在表模式中的索引
  ComparisonType op;
  Value val;
};

static std::optional<AtomicPredInfo> AsColumnOpConst(const std::shared_ptr<OperatorExpression> &pred) {
  if (pred->GetExprType() != OperatorExpressionType::COMPARISON) return std::nullopt;
  auto cmp = std::dynamic_pointer_cast<Comparison>(pred);
  auto ct = cmp->GetComparisonType();
  if (ct != ComparisonType::EQUAL && ct != ComparisonType::GREATER &&
      ct != ComparisonType::GREATER_EQUAL && ct != ComparisonType::LESS &&
      ct != ComparisonType::LESS_EQUAL) {
    return std::nullopt;
  }
  auto &ch = cmp->children_;
  if (ch[0]->GetExprType() == OperatorExpressionType::COLUMN_VALUE &&
      ch[1]->GetExprType() == OperatorExpressionType::CONST) {
    auto col = std::dynamic_pointer_cast<ColumnValue>(ch[0]);
    auto c = std::dynamic_pointer_cast<Const>(ch[1]);
    return AtomicPredInfo{col->GetColumnIndex(), ct, c->value_};
  }
  return std::nullopt;
}

static std::shared_ptr<Operator> WrapWithFilters(std::shared_ptr<Operator> plan, const std::vector<std::shared_ptr<OperatorExpression>> &filters) {
  for (auto &f : filters) {
    plan = std::make_shared<FilterOperator>(plan->column_list_, plan, f);
  }
  return plan;
}
// 收集 SeqScan 之上的 Filter 堆栈，返回的谓词按自底向上顺序（最内层 Filter 在前）
static void CollectFilterStackHelper(std::shared_ptr<Operator> plan, std::vector<std::shared_ptr<OperatorExpression>> &preds, std::shared_ptr<SeqScanOperator> &seqscan) {
  if (plan->GetType() == OperatorType::SEQSCAN) {
    seqscan = std::dynamic_pointer_cast<SeqScanOperator>(plan);
    return;
  }
  if (plan->GetType() == OperatorType::FILTER) {
    auto filter = std::dynamic_pointer_cast<FilterOperator>(plan);
    CollectFilterStackHelper(filter->children_[0], preds, seqscan);
    if (seqscan != nullptr) {
      preds.push_back(filter->predicate_);
    }
  }
}

// 尝试用 BTreeIndex 从 DNF 项构建扫描段
// col_indices：索引列在表模式中的位置（从左到右）
// 成功时，pushed 填入被索引消耗的谓词，residual 填入剩余谓词；若无可下推谓词则返回 nullptr
static std::shared_ptr<Segment> TryBuildSegment(const std::vector<std::shared_ptr<OperatorExpression>> &term, const std::vector<size_t> &col_indices, std::vector<std::shared_ptr<OperatorExpression>> &pushed, std::vector<std::shared_ptr<OperatorExpression>> &residual) {
  std::vector<Value> left_vals, right_vals;
  bool left_eq = true, right_eq = true;
  std::set<size_t> pushed_indices;
  for (size_t ic = 0; ic < col_indices.size(); ++ic) {
    size_t col_pos = col_indices[ic];
    // 收集该列的谓词
    std::shared_ptr<OperatorExpression> eq_pred = nullptr;
    std::shared_ptr<OperatorExpression> lower_pred = nullptr;
    std::shared_ptr<OperatorExpression> upper_pred = nullptr;
    Value lower_val, upper_val;
    bool lower_eq_flag = false, upper_eq_flag = false;
    for (size_t pi = 0; pi < term.size(); ++pi) {
      if (pushed_indices.count(pi)) continue;
      auto info = AsColumnOpConst(term[pi]);
      if (!info || info->col_idx != col_pos) continue;
      if (info->op == ComparisonType::EQUAL) {
        if (!eq_pred) { eq_pred = term[pi]; lower_val = upper_val = info->val; }
      } else if (info->op == ComparisonType::GREATER || info->op == ComparisonType::GREATER_EQUAL) {
        if (!lower_pred) {
          lower_pred = term[pi];
          lower_val = info->val;
          lower_eq_flag = (info->op == ComparisonType::GREATER_EQUAL);
        }
      } else if (info->op == ComparisonType::LESS || info->op == ComparisonType::LESS_EQUAL) {
        if (!upper_pred) {
          upper_pred = term[pi];
          upper_val = info->val;
          upper_eq_flag = (info->op == ComparisonType::LESS_EQUAL);
        }
      }
    }
    if (eq_pred) {
      // 等值：推进左右前缀
      for (size_t pi = 0; pi < term.size(); ++pi) {
        if (pushed_indices.count(pi)) continue;
        auto info = AsColumnOpConst(term[pi]);
        if (info && info->col_idx == col_pos && info->op == ComparisonType::EQUAL) {
          pushed_indices.insert(pi);
          break;
        }
      }
      left_vals.push_back(lower_val);
      right_vals.push_back(upper_val);
      // left_eq 和 right_eq 保持 true
    } else if (lower_pred || upper_pred) {
      if (lower_pred) {
        for (size_t pi = 0; pi < term.size(); ++pi) {
          if (pushed_indices.count(pi)) continue;
          auto info = AsColumnOpConst(term[pi]);
          if (info && info->col_idx == col_pos &&
              (info->op == ComparisonType::GREATER || info->op == ComparisonType::GREATER_EQUAL)) {
            pushed_indices.insert(pi);
            break;
          }
        }
        left_vals.push_back(lower_val);
        left_eq = lower_eq_flag;
      }
      if (upper_pred) {
        for (size_t pi = 0; pi < term.size(); ++pi) {
          if (pushed_indices.count(pi)) continue;
          auto info = AsColumnOpConst(term[pi]);
          if (info && info->col_idx == col_pos &&
              (info->op == ComparisonType::LESS || info->op == ComparisonType::LESS_EQUAL)) {
            pushed_indices.insert(pi);
            break;
          }
        }
        right_vals.push_back(upper_val);
        right_eq = upper_eq_flag;
      }
      break; // 范围谓词后停止处理更多索引列
    } else {
      break; // 该列无可用谓词：停止
    }
  }

  if (pushed_indices.empty()) {
    // 无下推谓词：无法使用索引
    residual = term;
    return nullptr;
  }

  for (size_t pi = 0; pi < term.size(); ++pi) {
    if (pushed_indices.count(pi)) {
      pushed.push_back(term[pi]);
    } else {
      residual.push_back(term[pi]);
    }
  }
  return std::make_shared<Segment>(left_vals, left_eq, right_vals, right_eq);
}

// 使用最佳可用 BTreeIndex 为单个 DNF 项构建子plan
static std::shared_ptr<Operator> BuildTermPlan( const std::vector<std::shared_ptr<OperatorExpression>> &term, const std::shared_ptr<SeqScanOperator> &seqscan, Catalog &catalog) {
  auto table_oid = seqscan->GetTableOid();
  auto indexes = catalog.GetTableIndex(table_oid);
  for (auto &idx : indexes) {
    auto btree_idx = std::dynamic_pointer_cast<BTreeIndex>(idx);
    if (!btree_idx) continue;
    auto col_indices = btree_idx->GetColumnIndices();
    std::vector<std::shared_ptr<OperatorExpression>> pushed, residual;
    auto segment = TryBuildSegment(term, col_indices, pushed, residual);
    if (!segment) continue;
    // 构建 BTreeScanOperator
    auto btree_scan = std::make_shared<BTreeScanOperator>(
        seqscan->column_list_, table_oid, seqscan->GetTableName(),
        seqscan->GetAlias(), seqscan->HasLock(), btree_idx, segment);
    return WrapWithFilters(btree_scan, residual);
  }
  // 无适用索引：用 SeqScan 配合所有谓词作为 Filter
  return WrapWithFilters(seqscan, term);
}
// 递归检查计划中是否包含 BTreeScan
static bool ContainsBTreeScan(const std::shared_ptr<Operator> &plan) {
  if (!plan) return false;
  if (plan->GetType() == OperatorType::BTREESCAN) return true;
  for (const auto &child : plan->children_) {
    if (ContainsBTreeScan(child)) return true;
  }
  return false;
}

std::shared_ptr<Operator> Optimizer::Seq2Index(std::shared_ptr<Operator> plan) {
  // LAB 6 BEGIN
  // 对于 Filter 节点：尝试检测并处理 [Filter...] SeqScan 模式
  if (plan->GetType() == OperatorType::FILTER) {
    std::vector<std::shared_ptr<OperatorExpression>> preds;
    std::shared_ptr<SeqScanOperator> seqscan;
    CollectFilterStackHelper(plan, preds, seqscan);
    if (seqscan != nullptr) {
      auto dnf_terms = ExpandToDNF(preds);// 将谓词展开为 DNF
      // 为每个DNF项尝试构建子plan，并检查是否使用了BTreeScan
      std::vector<std::shared_ptr<Operator>> sub_plans;
      bool any_btree = false;
      for (auto &term : dnf_terms) {
        auto sub_plan = BuildTermPlan(term, seqscan, catalog_);
        if (ContainsBTreeScan(sub_plan)) {
          any_btree = true;
        }
        sub_plans.push_back(sub_plan);
      }
      if (!any_btree) {
        // 无优化可能：返回原始plan
        return plan;
      }
      if (sub_plans.size() == 1) {
        return sub_plans[0];
      }
      // 多项：用 RidUnion 包装
      return std::make_shared<RidUnionOperator>(seqscan->column_list_, sub_plans);
    }
    // 子节点不是 SeqScan 链；继续递归处理
  }
  // 递归处理子节点
  for (auto &child : plan->children_) {
    child = Seq2Index(child);
  }
  return plan;
}

}  // namespace huadb
