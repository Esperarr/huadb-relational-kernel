#include "executors/rid_union_executor.h"

namespace huadb {

RidUnionExecutor::RidUnionExecutor(ExecutorContext &context, std::shared_ptr<const RidUnionOperator> plan,
                                   std::vector<std::shared_ptr<Executor>> &children)
    : Executor(context, std::move(children)), plan_(std::move(plan)) {}

void RidUnionExecutor::Init() {
  // LAB 6 BEGIN
}

std::shared_ptr<Record> RidUnionExecutor::Next() {
  // LAB 6 BEGIN
}

}  // namespace huadb
