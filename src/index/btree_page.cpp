#include "index/btree_page.h"

#include <iostream>

#include "common/constants.h"
#include "common/exceptions.h"
#include "table/record.h"

namespace huadb {

void BTreePage::Init() {
  data_->page_lsn_ = NULL_LSN;
  // LAB 6 BEGIN
}

bool BTreePage::IsLeafPage() const { return data_->page_type_ == BTreePageType::LEAF; }

void BTreePage::SetPageLSN(lsn_t lsn) {
  data_->page_lsn_ = lsn;
  page_->SetDirty();
}

void BTreePage::SetPageType(BTreePageType page_type) {
  data_->page_type_ = page_type;
  page_->SetDirty();
}

db_size_t BTreePage::GetRecordCount() const { return (data_->lower_ - sizeof(BTreePageData)) / sizeof(Slot); }

db_size_t BTreePage::GetFreeSpaceSize() const {
  if (data_->upper_ < data_->lower_ + sizeof(Slot)) {
    return 0;
  } else {
    return data_->upper_ - data_->lower_ - sizeof(Slot);
  }
}

db_size_t BTreePage::AddRecord(std::shared_ptr<IndexRecord> record, slotid_t slot_id) {
  // Step1. 检查是否有空余空间
  auto record_size = record->GetSize();
  if (record_size > GetFreeSpaceSize()) {
    throw DbException("Record size too large");
  }

  // Step2. 检查offset是否有效，如果无效就插在最后
  auto record_count = GetRecordCount();
  if (slot_id == INVALID_SLOT) {
    slot_id = record_count;
  }

  // Step3. 插入记录，需要用memmove()将slots_数组整理好
  // LAB 6 BEGIN

  // Step4. 返回插入记录的 slot_id，别忘了page_->SetDirty();
  // LAB 6 BEGIN
}

std::shared_ptr<IndexRecord> BTreePage::GetRecord(slotid_t slot_id, ColumnList schema) {
  auto index_record = std::make_shared<IndexRecord>();
  index_record->DeserializeFrom(reinterpret_cast<char *>(data_) + data_->slots_[slot_id].offset_, schema);
  return index_record;
}

void *BTreePage::GetSpecialData() const { return reinterpret_cast<char *>(data_) + data_->special_; }

char *BTreePage::GetData() const { return reinterpret_cast<char *>(data_); }

pageid_t BTreePage::GetPageId() const { return page_id_; }

}  // namespace huadb
