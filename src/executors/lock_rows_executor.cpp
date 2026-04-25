#include "executors/lock_rows_executor.h"

namespace huadb {

LockRowsExecutor::LockRowsExecutor(ExecutorContext &context, std::shared_ptr<const LockRowsOperator> plan,
                                   std::shared_ptr<Executor> child)
    : Executor(context, {std::move(child)}), plan_(std::move(plan)) {}

void LockRowsExecutor::Init() { children_[0]->Init(); }

std::shared_ptr<Record> LockRowsExecutor::Next() {
  auto record = children_[0]->Next();
  if (record == nullptr) {
    return nullptr;
  }
  // 根据 plan_ 的 lock type 获取正确的锁，加锁失败时抛出异常
  // LAB 3 BEGIN
  auto lock_type = plan_->GetLockType();
  if (lock_type == SelectLockType::SHARE) {
    if (!context_.GetLockManager().LockTable(context_.GetXid(), LockType::IS, plan_->GetOid())) {
      throw DbException("LockRowsExecutor: lock table IS failed");
    }
    if (!context_.GetLockManager().LockRow(context_.GetXid(), LockType::S, plan_->GetOid(), record->GetRid())) {
      throw DbException("LockRowsExecutor: lock row S failed");
    }
  } else if (lock_type == SelectLockType::UPDATE) {
    if (!context_.GetLockManager().LockTable(context_.GetXid(), LockType::IX, plan_->GetOid())) {
      throw DbException("LockRowsExecutor: lock table IX failed");
    }
    if (!context_.GetLockManager().LockRow(context_.GetXid(), LockType::X, plan_->GetOid(), record->GetRid())) {
      throw DbException("LockRowsExecutor: lock row X failed");
    }
  }
  // SelectLockType::NOLOCK 不加锁
  return record;
}

}  // namespace huadb
