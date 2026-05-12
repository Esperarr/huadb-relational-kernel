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

std::shared_ptr<Operator> Optimizer::PushDownPredicates(std::shared_ptr<Operator> plan) {
  // LAB 5 BEGIN
  return plan;
}

}  // namespace huadb
