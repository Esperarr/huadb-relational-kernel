#include "executors/seqscan_executor.h"

namespace huadb {

SeqScanExecutor::SeqScanExecutor(ExecutorContext &context, std::shared_ptr<const SeqScanOperator> plan)
    : Executor(context, {}), plan_(std::move(plan)) {}

void SeqScanExecutor::Init() {
  auto table = context_.GetCatalog().GetTable(plan_->GetTableOid());
  scan_ = std::make_unique<TableScan>(context_.GetBufferPool(), table, Rid{table->GetFirstPageId(), 0});
}

std::shared_ptr<Record> SeqScanExecutor::Next() {
  std::unordered_set<xid_t> active_xids;
  // 根据隔离级别，获取活跃事务的 xid（通过 context_ 获取需要的信息）
  // 通过 context_ 获取正确的锁，加锁失败时抛出异常
  // LAB 3 BEGIN
  auto isolation_level = context_.GetIsolationLevel();
  auto &txn_mgr = context_.GetTransactionManager();
  if (isolation_level == IsolationLevel::REPEATABLE_READ ||
      isolation_level == IsolationLevel::SERIALIZABLE) {
    // RR / SER：使用事务开始时的快照
    active_xids = txn_mgr.GetSnapshot(context_.GetXid());
  } else if (isolation_level == IsolationLevel::READ_COMMITTED) {
    // RC：使用当前时刻的活跃事务集合
    active_xids = txn_mgr.GetActiveTransactions();
  }
  // SS2PL/可串行化下，对扫描的表加 IS 锁
  if (isolation_level == IsolationLevel::SERIALIZABLE) {
    if (!context_.GetLockManager().LockTable(context_.GetXid(), LockType::IS, plan_->GetTableOid())) {
      throw DbException("SeqScanExecutor: lock table IS failed");
    }
  }
  return scan_->GetNextRecord(context_.GetXid(), context_.GetIsolationLevel(), context_.GetCid(), active_xids);
}

}  // namespace huadb
