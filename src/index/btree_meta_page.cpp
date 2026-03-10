#include "index/btree_meta_page.h"

#include "index/btree_page.h"

namespace huadb {

pageid_t BTreeMetaPage::GetRootPageId() const { return GetSpecialData()->root_page_id_; }

void BTreeMetaPage::SetRootPageId(pageid_t root_page_id) {
  GetSpecialData()->root_page_id_ = root_page_id;
  page_->SetDirty();
}

void BTreeMetaPage::Init() {
  BTreePage::Init();
  data_->special_ = data_->upper_ = data_->lower_;  // 这样插入元组会报错
  data_->page_type_ = BTreePageType::META;
  GetSpecialData()->root_page_id_ = NULL_PAGE_ID;
}

BTreeMetaPageData *BTreeMetaPage::GetSpecialData() const {
  return reinterpret_cast<BTreeMetaPageData *>(BTreePage::GetSpecialData());
}

};  // namespace huadb
