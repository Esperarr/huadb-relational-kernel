#pragma once

#include "btree_page.h"

namespace huadb {

class BTreeInternalPageData {
 public:
  BTreeInternalPageData() = delete;
};

class BTreeInternalPage : public BTreePage {
 public:
  explicit BTreeInternalPage(std::shared_ptr<Page> page, pageid_t page_id) : BTreePage(page, page_id) {}
  void Init();

 private:
  BTreeInternalPageData *GetSpecialData() const;
};

}  // namespace huadb
