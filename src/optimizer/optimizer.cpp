#include "optimizer/optimizer.h"

namespace huadb {

Optimizer::Optimizer(Catalog &catalog, JoinOrderAlgorithm join_order_algorithm, bool enable_projection_pushdown,
                     ForceJoin force_join)
    : catalog_(catalog),
      join_order_algorithm_(join_order_algorithm),
      enable_projection_pushdown_(enable_projection_pushdown),
      force_join_(force_join) {}

std::shared_ptr<Operator> Optimizer::Optimize(std::shared_ptr<Operator> plan) {
  plan = NormalizePredicates(plan);
  plan = PushDownPredicates(plan);
  plan = ReorderJoin(plan);

  // 只针对只读查询进行索引优化
  // LAB 6 BEGIN
  return plan;
}

}  // namespace huadb
