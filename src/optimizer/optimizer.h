#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "catalog/catalog.h"
#include "operators/expressions/column_value.h"
#include "operators/expressions/comparison.h"
#include "operators/operator.h"
#include "planner/planner.h"  // ForceJoin enum

namespace huadb {

enum class JoinOrderAlgorithm { NONE, DP, GREEDY };
static constexpr JoinOrderAlgorithm DEFAULT_JOIN_ORDER_ALGORITHM = JoinOrderAlgorithm::NONE;

class Optimizer {
 public:
  Optimizer(Catalog &catalog, JoinOrderAlgorithm join_order_algorithm, bool enable_projection_pushdown,
            ForceJoin force_join);
  std::shared_ptr<Operator> Optimize(std::shared_ptr<Operator> plan);

 private:
  // ---- Pass 入口（按 Optimize 流水线顺序）----
  std::shared_ptr<Operator> NormalizePredicates(std::shared_ptr<Operator> plan);
  std::shared_ptr<Operator> PushDownPredicates(std::shared_ptr<Operator> plan);
  std::shared_ptr<Operator> ReorderJoin(std::shared_ptr<Operator> plan);
  std::shared_ptr<Operator> Seq2Index(std::shared_ptr<Operator> plan);

  JoinOrderAlgorithm join_order_algorithm_;
  bool enable_projection_pushdown_;
  ForceJoin force_join_;
  Catalog &catalog_;
};

}  // namespace huadb
