#include "common/exceptions.h"
#include "operators/expressions/comparison.h"
#include "operators/expressions/logic.h"
#include "operators/filter_operator.h"
#include "optimizer/optimizer.h"

namespace huadb {

// 你可以定义一些辅助函数
// LAB 5 BEGIN
// 步骤1辅助：翻转比较符（用于交换 const op col → col op const）
static ComparisonType FlipComparisonType(ComparisonType t) {
  switch (t) {
    case ComparisonType::LESS:          return ComparisonType::GREATER;
    case ComparisonType::LESS_EQUAL:    return ComparisonType::GREATER_EQUAL;
    case ComparisonType::GREATER:       return ComparisonType::LESS;
    case ComparisonType::GREATER_EQUAL: return ComparisonType::LESS_EQUAL;
    default:                            return t;  // = 和 != 不变
  }
}

// 步骤2辅助：取反比较符（用于消除 NOT 作用于 Comparison 的情形）
static ComparisonType NegateComparisonType(ComparisonType t) {
  switch (t) {
    case ComparisonType::EQUAL:         return ComparisonType::NOT_EQUAL;
    case ComparisonType::NOT_EQUAL:     return ComparisonType::EQUAL;
    case ComparisonType::LESS:          return ComparisonType::GREATER_EQUAL;
    case ComparisonType::LESS_EQUAL:    return ComparisonType::GREATER;
    case ComparisonType::GREATER:       return ComparisonType::LESS_EQUAL;
    case ComparisonType::GREATER_EQUAL: return ComparisonType::LESS;
    default:
      throw DbException("NegateComparisonType: unsupported type");
  }
}

// 步骤1：递归将 const op col 翻转为 col op const
static std::shared_ptr<OperatorExpression> NormalizeCompOrder(std::shared_ptr<OperatorExpression> expr) {
  if (expr->GetExprType() == OperatorExpressionType::COMPARISON) {
    auto cmp = std::dynamic_pointer_cast<Comparison>(expr);
    // 先递归处理子节点（子节点也可能有 const op col，虽然题目中不会出现嵌套，但做通用处理）
    for (auto &child : cmp->children_) {
      child = NormalizeCompOrder(child);
    }
    // 如果 left=CONST, right=COLUMN_VALUE，则交换两孩子并翻转比较符
    if (cmp->children_[0]->GetExprType() == OperatorExpressionType::CONST &&
        cmp->children_[1]->GetExprType() == OperatorExpressionType::COLUMN_VALUE) {
      return std::make_shared<Comparison>(FlipComparisonType(cmp->GetComparisonType()),
                                          cmp->children_[1], cmp->children_[0]);
    }
    return cmp;
  }
  // 对 Logic 节点递归
  for (auto &child : expr->children_) {
    child = NormalizeCompOrder(child);
  }
  return expr;
}

// 步骤2：递归消除 NOT 节点（德摩根 + NOT-Comparison: 取反比较符）
static std::shared_ptr<OperatorExpression> EliminateNot(std::shared_ptr<OperatorExpression> expr) {
  if (expr->GetExprType() != OperatorExpressionType::LOGIC) {
    return expr;
  }
  auto logic = std::dynamic_pointer_cast<Logic>(expr);
  if (logic->GetLogicType() == LogicType::NOT) {
    auto child = logic->children_[0];
    // NOT(Comparison) → 取反比较符
    if (child->GetExprType() == OperatorExpressionType::COMPARISON) {
      auto cmp = std::dynamic_pointer_cast<Comparison>(child);
      return std::make_shared<Comparison>(NegateComparisonType(cmp->GetComparisonType()),
                                          cmp->children_[0], cmp->children_[1]);
    }
    if (child->GetExprType() == OperatorExpressionType::LOGIC) {
      auto child_logic = std::dynamic_pointer_cast<Logic>(child);
      if (child_logic->GetLogicType() == LogicType::AND) {
        // NOT(A AND B) → NOT(A) OR NOT(B)
        auto not_a = std::make_shared<Logic>(LogicType::NOT, child_logic->children_[0]);
        auto not_b = std::make_shared<Logic>(LogicType::NOT, child_logic->children_[1]);
        return EliminateNot(std::make_shared<Logic>(LogicType::OR, not_a, not_b));
      }
      if (child_logic->GetLogicType() == LogicType::OR) {
        // NOT(A OR B) → NOT(A) AND NOT(B)
        auto not_a = std::make_shared<Logic>(LogicType::NOT, child_logic->children_[0]);
        auto not_b = std::make_shared<Logic>(LogicType::NOT, child_logic->children_[1]);
        return EliminateNot(std::make_shared<Logic>(LogicType::AND, not_a, not_b));
      }
      if (child_logic->GetLogicType() == LogicType::NOT) {
        // NOT(NOT(A)) → A
        return EliminateNot(child_logic->children_[0]);
      }
    }
    // 其他情况
    return expr;
  }
  // AND / OR：递归子节点
  for (auto &child : logic->children_) {
    child = EliminateNot(child);
  }
  return logic;
}

// 步骤3辅助：把顶层 AND 拆开，收集到 parts 中
static void CollectTopLevelAnd(std::shared_ptr<OperatorExpression> expr,
                                std::vector<std::shared_ptr<OperatorExpression>> &parts) {
  if (expr->GetExprType() == OperatorExpressionType::LOGIC) {
    auto logic = std::dynamic_pointer_cast<Logic>(expr);
    if (logic->GetLogicType() == LogicType::AND) {
      CollectTopLevelAnd(logic->children_[0], parts);
      CollectTopLevelAnd(logic->children_[1], parts);
      return;
    }
  }
  parts.push_back(expr);
}

std::shared_ptr<Operator> Optimizer::NormalizePredicates(std::shared_ptr<Operator> plan) {
  // 先递归处理子树：保证从叶子向上 normalize；遇到嵌套 Filter 时父层先看到的是
  // 已 normalize 过的子树，便于本层把拆出的多个 Filter 串到 normalized child 上。
  for (auto &child : plan->children_) {
    child = NormalizePredicates(child);
  }
  if (plan->GetType() != OperatorType::FILTER) {
    return plan;
  }
  const auto filter = std::dynamic_pointer_cast<FilterOperator>(plan);

  // LAB 5 BEGIN
  // 步骤1：将 const op col 改写为 col op const
  filter->predicate_ = NormalizeCompOrder(filter->predicate_);
  // 步骤2：消除 NOT
  filter->predicate_ = EliminateNot(filter->predicate_);
  // 步骤3：拆分顶层 AND 为多个 FilterOperator
  std::vector<std::shared_ptr<OperatorExpression>> parts;
  CollectTopLevelAnd(filter->predicate_, parts);
  if (parts.size() == 1) {
    return filter;// 没有顶层 AND，不需要拆分
  }
  // 自下而上叠在 filter->children_[0] 之上
  std::shared_ptr<Operator> result = filter->children_[0];
  for (auto &part : parts) {
    auto col_list = std::make_shared<ColumnList>(result->OutputColumns());
    result = std::make_shared<FilterOperator>(col_list, result, part);
  }
  return result;
}

}  // namespace huadb