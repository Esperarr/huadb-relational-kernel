#include <algorithm>
#include <cassert>
#include <map>
#include <set>

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

std::shared_ptr<Operator> Optimizer::Seq2Index(std::shared_ptr<Operator> plan) {
  // LAB 6 BEGIN
  return plan;
}

}  // namespace huadb
