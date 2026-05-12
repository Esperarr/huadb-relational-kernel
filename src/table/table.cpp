#include "table/table.h"

#include "storage/disk.h"
#include "table/table_page.h"

namespace huadb {

Table::Table(BufferPool &buffer_pool, LogManager &log_manager, Disk &disk, oid_t oid, oid_t db_oid,
             ColumnList column_list, bool new_table, bool is_empty)
    : buffer_pool_(buffer_pool),
      log_manager_(log_manager),
      disk_(disk),
      oid_(oid),
      db_oid_(db_oid),
      column_list_(std::move(column_list)) {
  if (new_table || is_empty) {
    first_page_id_ = NULL_PAGE_ID;
    last_page_id_ = NULL_PAGE_ID;
  } else {
    first_page_id_ = 0;
    last_page_id_ = disk_.GetPageCount(disk_.GetFilePath(db_oid, oid)) - 1;
  }
}

Rid Table::InsertRecord(std::shared_ptr<Record> record, xid_t xid, cid_t cid, bool write_log) {
  if (record->GetSize() > MAX_RECORD_SIZE) {
    throw DbException("Record size too large: " + std::to_string(record->GetSize()));
  }

  // 当 write_log 参数为 true 时开启写日志功能
  // 在插入记录时增加写 InsertLog 过程
  // 在创建新的页面时增加写 NewPageLog 过程
  // 设置页面的 page lsn
  // LAB 2 BEGIN

  // 使用 buffer_pool_ 获取页面
  // 使用 TablePage 类操作记录页面
  // 遍历表的页面，判断页面是否有足够的空间插入记录，如果没有则通过 buffer_pool_ 创建新页面
  // 如果 first_page_id_ 为 NULL_PAGE_ID，说明表还没有页面，需要创建新页面
  // 创建新页面时需设置前一个页面的 next_page_id，并将新页面初始化
  // 找到空间足够的页面后，通过 TablePage 插入记录
  // 返回插入记录的 rid
  // LAB 1 BEGIN
  db_size_t record_size = record->GetSize();
  // 表还没有任何页面，先创建第一个页面
  if (first_page_id_ == NULL_PAGE_ID) {
    first_page_id_ = 0;
    auto page = buffer_pool_.NewPage(db_oid_, oid_, 0);
    TablePage table_page(page);
    table_page.Init();
    if (write_log) {
      // 写新建页面日志（prev_page_id 为 NULL_PAGE_ID）
      lsn_t lsn = log_manager_.AppendNewPageLog(xid, oid_, NULL_PAGE_ID, 0);
      table_page.SetPageLSN(lsn);
    }
    slotid_t slot_id = table_page.InsertRecord(record, xid, cid);
    if (write_log) {
      lsn_t lsn = log_manager_.AppendInsertLog(xid, oid_, 0, slot_id,
                                                table_page.GetUpper(),
                                                record_size,
                                                page->GetData() + table_page.GetUpper());
      table_page.SetPageLSN(lsn);
    }
    return Rid{0, slot_id};
  }

  // 遍历链表找到第一个有空间的页面，同时记录链表最后一页的 page_id
  pageid_t target_page_id = NULL_PAGE_ID;  // 找到的有空间的页面
  pageid_t last_page_id = first_page_id_;  // 链表最后一页

  pageid_t cur = first_page_id_;
  while (cur != NULL_PAGE_ID) {
    auto page = buffer_pool_.GetPage(db_oid_, oid_, cur);
    TablePage table_page(page);
    // 找到第一个有空间的页面就记下来，但继续遍历找到链表尾
    if (target_page_id == NULL_PAGE_ID && table_page.GetFreeSpaceSize() >= record_size) {
      target_page_id = cur;
    }
    last_page_id = cur;
    cur = table_page.GetNextPageId();
  }
  // 找到了有空间的页面，直接插入
  if (target_page_id != NULL_PAGE_ID) {
    auto page = buffer_pool_.GetPage(db_oid_, oid_, target_page_id);
    TablePage table_page(page);
    slotid_t slot_id = table_page.InsertRecord(record, xid, cid);
    if (write_log) {
      lsn_t lsn = log_manager_.AppendInsertLog(xid, oid_, target_page_id, slot_id,
                                                table_page.GetUpper(),
                                                record_size,
                                                page->GetData() + table_page.GetUpper());
      table_page.SetPageLSN(lsn);
    }
    return Rid{target_page_id, slot_id};
  }
  // 所有页面都满，在链表尾追加新页面
  else{
    pageid_t new_id = last_page_id + 1;
    auto last_page = buffer_pool_.GetPage(db_oid_, oid_, last_page_id);
    TablePage last_table_page(last_page);
    last_table_page.SetNextPageId(new_id);
    auto new_page = buffer_pool_.NewPage(db_oid_, oid_, new_id);
    TablePage new_table_page(new_page);
    new_table_page.Init();
    if (write_log) {
      lsn_t lsn = log_manager_.AppendNewPageLog(xid, oid_, last_page_id, new_id);
      new_table_page.SetPageLSN(lsn);
      last_table_page.SetPageLSN(lsn);
    }
    slotid_t slot_id = new_table_page.InsertRecord(record, xid, cid);
    if (write_log) {
      lsn_t lsn = log_manager_.AppendInsertLog(xid, oid_, new_id, slot_id,
                                                new_table_page.GetUpper(),
                                                record_size,
                                                new_page->GetData() + new_table_page.GetUpper());
      new_table_page.SetPageLSN(lsn);
    }
    return Rid{new_id, slot_id};
  }
}

void Table::DeleteRecord(const Rid &rid, xid_t xid, bool write_log) {
  // 增加写 DeleteLog 过程
  // 设置页面的 page lsn
  // LAB 2 BEGIN

  // 使用 TablePage 操作页面
  // LAB 1 BEGIN
  std::shared_ptr<Page> page = buffer_pool_.GetPage(db_oid_, oid_, rid.page_id_);
  auto table_page = std::make_unique<TablePage>(page);
  if (write_log) {
    lsn_t lsn = log_manager_.AppendDeleteLog(xid, oid_, rid.page_id_, rid.slot_id_);
    table_page->SetPageLSN(lsn);
  }
  table_page->DeleteRecord(rid.slot_id_, xid);
}

Rid Table::UpdateRecord(const Rid &rid, xid_t xid, cid_t cid, std::shared_ptr<Record> record, bool write_log) {
  DeleteRecord(rid, xid, write_log);
  return InsertRecord(record, xid, cid, write_log);
}

void Table::UpdateRecordInPlace(const Record &record) {
  auto rid = record.GetRid();
  auto table_page = std::make_unique<TablePage>(buffer_pool_.GetPage(db_oid_, oid_, rid.page_id_));
  table_page->UpdateRecordInPlace(record, rid.slot_id_);
}

pageid_t Table::GetFirstPageId() const { return first_page_id_; }

oid_t Table::GetOid() const { return oid_; }

oid_t Table::GetDbOid() const { return db_oid_; }

const ColumnList &Table::GetColumnList() const { return column_list_; }

}  // namespace huadb
