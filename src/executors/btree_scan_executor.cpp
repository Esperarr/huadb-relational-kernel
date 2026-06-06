#include "executors/btree_scan_executor.h"

#include "index/btree_leaf_page.h"
#include "table/table_page.h"

namespace huadb {

// 比较用户键向量，比较长度取 a 和 b 中较短者
static int CompareUserKeyPrefix(const std::vector<Value> &a, const std::vector<Value> &b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    if (a[i].Less(b[i])) return -1;
    if (b[i].Less(a[i])) return 1;
  }
  return 0;
}

BTreeScanExecutor::BTreeScanExecutor(ExecutorContext &context, std::shared_ptr<const BTreeScanOperator> plan)
    : Executor(context, {}), plan_(std::move(plan)), table_(context_.GetCatalog().GetTable(plan_->GetTableOid())) {}

void BTreeScanExecutor::Init() {
  // LAB 6 BEGIN
  auto &index = plan_->GetIndex();
  auto &segment = plan_->GetSegment();
  index_record_rid_ = index->Search(segment->left_val_, segment->left_eq_);
}

// 暂时不考虑并发控制、事务回滚、故障恢复
std::shared_ptr<Record> BTreeScanExecutor::Next() {
  // LAB 6 BEGIN
  auto &index = plan_->GetIndex();
  auto &segment = plan_->GetSegment();
  auto index_schema = index->GetColumns();
  while (true) {
    if (index_record_rid_.page_id_ == NULL_PAGE_ID) {
      return nullptr;
    }
    auto idx_page_id = index_record_rid_.page_id_;
    auto idx_slot_id = index_record_rid_.slot_id_;
    auto leaf_page = std::make_shared<BTreeLeafPage>(context_.GetBufferPool().GetPage(index->GetDbOid(), index->GetOid(), idx_page_id), idx_page_id);// 从索引文件中获取叶子页
    auto record_count = leaf_page->GetRecordCount();
    if (idx_slot_id >= record_count) {
      // 移动到下一个叶子页
      auto next_id = leaf_page->GetNextPageId();
      index_record_rid_ = {next_id, 0};
      continue;
    }
    auto index_record = leaf_page->GetRecord(idx_slot_id, *index_schema);// 获取当前位置的索引记录
    // 检查右边界
    const auto &leaf_vals = index_record->GetValues();
    const auto &right_val = segment->right_val_;
    if (!right_val.empty()) {
      int cmp = CompareUserKeyPrefix(leaf_vals, right_val);
      if (cmp > 0 || (cmp == 0 && !segment->right_eq_)) {
        index_record_rid_ = {NULL_PAGE_ID, 0};
        return nullptr;
      }
    }
    index_record_rid_ = {idx_page_id, static_cast<slotid_t>(idx_slot_id + 1)};// 前进到下一个位置
    // 使用 ptr_ 获取堆表记录
    auto heap_rid = index_record->GetPtr();
    TablePage heap_tp(context_.GetBufferPool().GetPage(table_->GetDbOid(), table_->GetOid(), heap_rid.page_id_));
    return heap_tp.GetRecord(heap_rid, table_->GetColumnList());
  }
}

}  // namespace huadb
