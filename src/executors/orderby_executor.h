#pragma once

#include "executors/executor.h"
#include "operators/orderby_operator.h"

namespace huadb {

class OrderByExecutor : public Executor {
 public:
  OrderByExecutor(ExecutorContext &context, std::shared_ptr<const OrderByOperator> plan,
                  std::shared_ptr<Executor> child);
  void Init() override;
  std::shared_ptr<Record> Next() override;

 private:
  std::shared_ptr<const OrderByOperator> plan_;
  bool sorted_ = false; // 是否已完成读取与排序
  size_t idx_ = 0; // 已输出的记录数
  std::vector<std::shared_ptr<Record>> records_; // 排序后的全部记录
};

}  // namespace huadb
