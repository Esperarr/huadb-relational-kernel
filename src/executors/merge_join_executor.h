#pragma once

#include "executors/executor.h"
#include "operators/merge_join_operator.h"

namespace huadb {

class MergeJoinExecutor : public Executor {
 public:
  MergeJoinExecutor(ExecutorContext &context, std::shared_ptr<const MergeJoinOperator> plan,
                    std::shared_ptr<Executor> left, std::shared_ptr<Executor> right);
  void Init() override;
  std::shared_ptr<Record> Next() override;

 private:
  std::shared_ptr<const MergeJoinOperator> plan_;
  bool right_loaded_ = false;
  std::vector<std::shared_ptr<Record>> right_records_;
  std::shared_ptr<Record> left_record_ = nullptr;
  size_t right_idx_ = 0;

  bool has_saved_group_ = false;
  size_t saved_right_idx_ = 0;
  Value saved_group_key_;
};

}  // namespace huadb
