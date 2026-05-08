#include "executors/merge_join_executor.h"

namespace huadb {

MergeJoinExecutor::MergeJoinExecutor(ExecutorContext &context, std::shared_ptr<const MergeJoinOperator> plan,
                                     std::shared_ptr<Executor> left, std::shared_ptr<Executor> right)
    : Executor(context, {std::move(left), std::move(right)}), plan_(std::move(plan)) {}

void MergeJoinExecutor::Init() {
  children_[0]->Init();
  children_[1]->Init();
}

std::shared_ptr<Record> MergeJoinExecutor::Next() {
  // LAB 4 BEGIN
  // 第一次调用时一次性把右侧全部读出
  if (!right_loaded_) {
    right_loaded_ = true;
    while (auto record = children_[1]->Next()) {
      right_records_.push_back(record);
    }
  }

  while (true) {
    // 取一个左元组；若已无左元组则归并结束
    if (left_record_ == nullptr) {
      left_record_ = children_[0]->Next();
      if (left_record_ == nullptr) {
        return nullptr;
      }
      // 若之前保存过一个匹配组，且新左元组的 key 仍等于该组的 key，说明左侧出现重复值，需要把右侧下标回退到组首与之重新匹配
      if (has_saved_group_) {
        Value new_left_key = plan_->left_key_->Evaluate(left_record_);
        if (new_left_key.Equal(saved_group_key_)) {
          right_idx_ = saved_right_idx_;
        } else {
          // 新左元组的 key 已经超过保存组的 key，匹配组失效
          has_saved_group_ = false;
          // 若右侧也已经扫到末尾，则不会再有任何匹配
          if (right_idx_ >= right_records_.size()) {
            return nullptr;
          }
        }
      }
    }

    // 标准归并比较：根据左右当前 key 的大小决定推进哪一侧
    while (right_idx_ < right_records_.size()) {
      Value left_val = plan_->left_key_->Evaluate(left_record_);
      Value right_val = plan_->right_key_->Evaluate(right_records_[right_idx_]);
      if (left_val.Less(right_val)) {
        // 左 < 右：左指针推进
        left_record_ = nullptr;
        break;
      } else if (left_val.Greater(right_val)) {
        // 左 > 右：右指针推进
        ++right_idx_;
      } else {
        // 左 == 右：命中
        // 第一次进入这个 key 的匹配组，记录回退点
        if (!has_saved_group_) {
          saved_right_idx_ = right_idx_;
          saved_group_key_ = right_val;
          has_saved_group_ = true;
        }
        // 拼接返回
        auto result = std::make_shared<Record>(*left_record_);
        result->Append(*right_records_[right_idx_]);
        ++right_idx_;
        return result;
      }
    }

    // 处理内层while结束情况
    if (right_idx_ >= right_records_.size() && left_record_ != nullptr) {
      if (has_saved_group_) {
        // 还有可能再次回退到保存组，取下一个左记录试试能否回退匹配
        left_record_ = nullptr;
      } else {
        // 右侧扫尽且没有可回退的组，归并结束
        return nullptr;
      }
    }
  }
}

}  // namespace huadb
