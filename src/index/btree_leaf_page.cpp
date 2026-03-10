#include "index/btree_leaf_page.h"

#include <iostream>

#include "common/constants.h"

namespace huadb {

void BTreeLeafPage::Init() {
  BTreePage::Init();
  data_->special_ = data_->upper_ = DB_PAGE_SIZE - sizeof(BTreeLeafPageData);
  data_->page_type_ = BTreePageType::LEAF;
  GetSpecialData()->prev_page_id_ = NULL_PAGE_ID;
  GetSpecialData()->next_page_id_ = NULL_PAGE_ID;
}

void BTreeLeafPage::Insert(std::shared_ptr<Record> record) {}

void BTreeLeafPage::Delete() {}

pageid_t BTreeLeafPage::GetPrevPageId() const { return GetSpecialData()->prev_page_id_; }

pageid_t BTreeLeafPage::GetNextPageId() const { return GetSpecialData()->next_page_id_; }

void BTreeLeafPage::SetPrevPageId(pageid_t prev_page_id) { GetSpecialData()->prev_page_id_ = prev_page_id; }

void BTreeLeafPage::SetNextPageId(pageid_t next_page_id) { GetSpecialData()->next_page_id_ = next_page_id; }

BTreeLeafPageData *BTreeLeafPage::GetSpecialData() const {
  return reinterpret_cast<BTreeLeafPageData *>(BTreePage::GetSpecialData());
}

}  // namespace huadb
