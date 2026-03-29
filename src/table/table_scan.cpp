#include "table/table_scan.h"

#include "table/table_page.h"

namespace huadb {

TableScan::TableScan(BufferPool &buffer_pool, std::shared_ptr<Table> table, Rid rid)
    : buffer_pool_(buffer_pool), table_(std::move(table)), rid_(rid) {}

std::shared_ptr<Record> TableScan::GetNextRecord(xid_t xid, IsolationLevel isolation_level, cid_t cid,
                                                 const std::unordered_set<xid_t> &active_xids) {
  // 根据事务隔离级别及活跃事务集合，判断记录是否可见
  // LAB 3 BEGIN

  // 每次调用读取一条记录
  // 读取时更新 rid_ 变量，避免重复读取
  // 扫描结束时，返回空指针
  // 注意处理扫描空表的情况（rid_.page_id_ 为 NULL_PAGE_ID）
  // LAB 1 BEGIN
  if (rid_.page_id_ == NULL_PAGE_ID) {
    return nullptr;
  }

  pageid_t page_id = rid_.page_id_;
  slotid_t slot_id = rid_.slot_id_;

  while (page_id != NULL_PAGE_ID) {
    auto page = buffer_pool_.GetPage(table_->GetDbOid(), table_->GetOid(), page_id);
    TablePage table_page(page);
    db_size_t total = table_page.GetRecordCount();
    // 从当前 rid_ 开始扫描，直到找到一条未删除的记录或扫描结束
    for (; rid_.slot_id_ < total; rid_.slot_id_++) {
      auto record = table_page.GetRecord(rid_, table_->GetColumnList());
      if (!record->IsDeleted()) {
        rid_.slot_id_++;   // 下次从下一条开始
        return record;
      }
    }
    // 移动到下一个页面
    page_id = table_page.GetNextPageId();
    slot_id = 0;
    rid_ = {page_id, slot_id};
  }
  return nullptr;
}

}  // namespace huadb
