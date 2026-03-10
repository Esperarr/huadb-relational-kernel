#pragma once

#include <set>

#include "executors/executor.h"
#include "operators/rid_union_operator.h"

namespace huadb {

class RidUnionExecutor : public Executor {
 public:
  RidUnionExecutor(ExecutorContext &context, std::shared_ptr<const RidUnionOperator> plan,
                   std::vector<std::shared_ptr<Executor>> &children);
  void Init() override;
  std::shared_ptr<Record> Next() override;

 private:
  std::shared_ptr<const RidUnionOperator> plan_;
  size_t current_child_index_ = 0;
  std::set<Rid> rids_;
};

}  // namespace huadb
