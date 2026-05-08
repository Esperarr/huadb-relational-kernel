#include "executors/nested_loop_join_executor.h"

namespace huadb {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext &context,
                                               std::shared_ptr<const NestedLoopJoinOperator> plan,
                                               std::shared_ptr<Executor> left, std::shared_ptr<Executor> right)
    : Executor(context, {std::move(left), std::move(right)}), plan_(std::move(plan)) {}

void NestedLoopJoinExecutor::Init() {
  children_[0]->Init();
  children_[1]->Init();
}

std::shared_ptr<Record> NestedLoopJoinExecutor::Next() {
  // 从 NestedLoopJoinOperator 中获取连接条件
  // 使用 OperatorExpression 的 EvaluateJoin 函数判断是否满足 join 条件
  // 使用 Record 的 Append 函数进行记录的连接
  // LAB 4 BEGIN
  // 第一次调用时把右侧全部物化，避免每次外层循环都重新 Init
  if (!right_loaded_) {
    right_loaded_ = true;
    while (auto record = children_[1]->Next()) {
      right_records_.push_back(record);
    }
  }

  // 外层循环：左侧元组；内层循环：右侧元组
  while (true) {
    // 取下一个左侧元组；左侧取尽则全部结束
    if (left_record_ == nullptr) {
      left_record_ = children_[0]->Next();// 取下一个新左侧元组
      if (left_record_ == nullptr) {
        return nullptr;
      }
      right_idx_ = 0;  // 新的左元组从右侧第0条开始扫描
    }
    // 在右侧扫描，找满足连接条件的记录
    while (right_idx_ < right_records_.size()) {
      auto right_record = right_records_[right_idx_++];
      Value cond = plan_->join_condition_->EvaluateJoin(left_record_, right_record);// 获取连接条件
      if (!cond.IsNull() && cond.GetValue<bool>()) {
        // 满足条件：把左右元组拼接成一个新记录并返回
        auto result = std::make_shared<Record>(*left_record_);
        result->Append(*right_record);
        return result;
      }
    }
    left_record_ = nullptr;// 当前左元组与右侧全部记录都比较完，换下一个左元组
  }
}

}  // namespace huadb
