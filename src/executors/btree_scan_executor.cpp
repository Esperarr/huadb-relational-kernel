#include "executors/btree_scan_executor.h"

#include "index/btree_leaf_page.h"
#include "table/table_page.h"

namespace huadb {

BTreeScanExecutor::BTreeScanExecutor(ExecutorContext &context, std::shared_ptr<const BTreeScanOperator> plan)
    : Executor(context, {}), plan_(std::move(plan)), table_(context_.GetCatalog().GetTable(plan_->GetTableOid())) {}

void BTreeScanExecutor::Init() {
  // LAB 6 BEGIN
}

// 暂时不考虑并发控制、事务回滚、故障恢复
std::shared_ptr<Record> BTreeScanExecutor::Next() {
  // LAB 6 BEGIN
}

}  // namespace huadb
