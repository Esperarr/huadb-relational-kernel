#include "executors/rid_union_executor.h"

namespace huadb {

RidUnionExecutor::RidUnionExecutor(ExecutorContext &context, std::shared_ptr<const RidUnionOperator> plan,
                                   std::vector<std::shared_ptr<Executor>> &children)
    : Executor(context, std::move(children)), plan_(std::move(plan)) {}

void RidUnionExecutor::Init() {
  // LAB 6 BEGIN
  for (auto &child : children_) {
    child->Init();
  }
  current_child_index_ = 0;
  rids_.clear();
}

std::shared_ptr<Record> RidUnionExecutor::Next() {
  // LAB 6 BEGIN
  while (current_child_index_ < children_.size()) {
    auto record = children_[current_child_index_]->Next();
    if (record == nullptr) {
      ++current_child_index_;
      continue;
    }
    auto rid = record->GetRid();
    if (rids_.find(rid) != rids_.end()) {
      // 已经返回
      continue;
    }
    rids_.insert(rid);
    return record;
  }
  return nullptr;
}

}  // namespace huadb
