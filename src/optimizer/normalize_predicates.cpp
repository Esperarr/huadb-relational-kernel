#include "common/exceptions.h"
#include "operators/expressions/comparison.h"
#include "operators/expressions/logic.h"
#include "operators/filter_operator.h"
#include "optimizer/optimizer.h"

namespace huadb {

// 你可以定义一些辅助函数
// LAB 5 BEGIN

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
  return plan;
}

}  // namespace huadb
