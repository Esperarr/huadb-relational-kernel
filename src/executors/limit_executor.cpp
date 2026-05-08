#include "executors/limit_executor.h"

namespace huadb {

LimitExecutor::LimitExecutor(ExecutorContext &context, std::shared_ptr<const LimitOperator> plan,
                             std::shared_ptr<Executor> child)
    : Executor(context, {std::move(child)}), plan_(std::move(plan)) {}

void LimitExecutor::Init() { children_[0]->Init(); }

std::shared_ptr<Record> LimitExecutor::Next() {
  // 通过 plan_ 获取 limit 语句中的 offset 和 limit 值
  // LAB 4 BEGIN
  // 第一次被调用时，先一次性跳过 offset 个记录（offset 缺省时跳过 0 个）
  if (!offset_skipped_) {
    offset_skipped_ = true;
    if (plan_->limit_offset_.has_value()) {
      for (uint32_t i = 0; i < plan_->limit_offset_.value(); ++i) {
        if (children_[0]->Next() == nullptr) {
          return nullptr;  // 子算子记录数不足 offset，直接结束
        }
      }
    }
  }
  // 已返回的记录数达到 limit 时停止，limit 缺省表示不设上限
  if (plan_->limit_count_.has_value() && count_ >= plan_->limit_count_.value()) {
    return nullptr;// 已达上限，结束
  }
  auto record = children_[0]->Next();
  if (record != nullptr) {
    ++count_;
  }
  return record;
}

}  // namespace huadb
