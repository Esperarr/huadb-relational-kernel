#include "index/btree_internal_page.h"

namespace huadb {

void BTreeInternalPage::Init() {
  BTreePage::Init();
  data_->special_ = data_->upper_ = DB_PAGE_SIZE - sizeof(BTreeInternalPageData);
  data_->page_type_ = BTreePageType::INTERNAL;
}

BTreeInternalPageData *BTreeInternalPage::GetSpecialData() const {
  return reinterpret_cast<BTreeInternalPageData *>(BTreePage::GetSpecialData());
}

}  // namespace huadb
