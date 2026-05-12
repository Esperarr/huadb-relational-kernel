#pragma once

#include "executors/executor.h"
#include "operators/btree_scan_operator.h"
#include "table/table.h"
#include "table/table_scan.h"

namespace huadb {

class BTreeScanExecutor : public Executor {
 public:
  BTreeScanExecutor(ExecutorContext &context, std::shared_ptr<const BTreeScanOperator> plan);

  void Init() override;
  std::shared_ptr<Record> Next() override;

 private:
  std::shared_ptr<const BTreeScanOperator> plan_;
  std::shared_ptr<Table> table_;  // 扫描的表
  Rid index_record_rid_;          // 当前扫描到的索引元组的 Rid
};

}  // namespace huadb
