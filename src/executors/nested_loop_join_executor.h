#pragma once

#include "executors/executor.h"
#include "operators/nested_loop_join_operator.h"

namespace huadb {

class NestedLoopJoinExecutor : public Executor {
 public:
  NestedLoopJoinExecutor(ExecutorContext &context, std::shared_ptr<const NestedLoopJoinOperator> plan,
                         std::shared_ptr<Executor> left, std::shared_ptr<Executor> right);
  void Init() override;
  std::shared_ptr<Record> Next() override;

 private:
  std::shared_ptr<const NestedLoopJoinOperator> plan_;
  bool right_loaded_ = false;// 是否已完成右侧物化
  std::vector<std::shared_ptr<Record>> right_records_;// 物化的右侧记录
  std::shared_ptr<Record> left_record_ = nullptr;// 当前外层左侧元组
  size_t right_idx_ = 0;// 内层扫描的右侧下标
};

}  // namespace huadb
