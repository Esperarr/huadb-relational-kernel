#pragma once

#include "executors/executor.h"
#include "operators/limit_operator.h"

namespace huadb {

class LimitExecutor : public Executor {
 public:
  LimitExecutor(ExecutorContext &context, std::shared_ptr<const LimitOperator> plan, std::shared_ptr<Executor> child);
  void Init() override;
  std::shared_ptr<Record> Next() override;

 private:
  std::shared_ptr<const LimitOperator> plan_;
  bool offset_skipped_ = false;  // 是否已完成首次 offset 跳过
  uint32_t count_ = 0;           // 已经向上层返回的记录数
};

}  // namespace huadb
