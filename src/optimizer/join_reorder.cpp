#include <algorithm>
#include <climits>
#include <limits>

#include "common/exceptions.h"
#include "common/string_util.h"
#include "operators/expressions/column_value.h"
#include "operators/expressions/comparison.h"
#include "operators/expressions/logic.h"
#include "operators/nested_loop_join_operator.h"
#include "operators/seqscan_operator.h"
#include "optimizer/optimizer.h"
#include "planner/planner.h"

namespace huadb {

// 你可以定义一些辅助函数
// LAB 5 BEGIN

std::shared_ptr<Operator> Optimizer::ReorderJoin(std::shared_ptr<Operator> plan) {
  // 通过 catalog_.GetCardinality 和 catalog_.GetDistinct 从系统表中读取表和列的元信息
  // 可根据 join_order_algorithm_ 变量的值选择不同的连接顺序选择算法，默认为 None，表示不进行连接顺序优化
  if (join_order_algorithm_ == JoinOrderAlgorithm::NONE) {
    return plan;  // 默认配置下连扫描都省了
  }
  // LAB 5 BEGIN
  return plan;
}

}  // namespace huadb
