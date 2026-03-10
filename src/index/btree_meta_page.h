#pragma once

#include "catalog/column_definition.h"
#include "catalog/column_list.h"
#include "common/types.h"
#include "index/btree_page.h"
#include "storage/page.h"

namespace huadb {

class BTreeMetaPageData {
 public:
  BTreeMetaPageData() = delete;  // 与BTreePageData同理
  pageid_t root_page_id_;        // b+树的根节点page_id，可以在这里扩展其他metadata
};

class BTreeMetaPage : public BTreePage {
 public:
  explicit BTreeMetaPage(std::shared_ptr<Page> page, pageid_t page_id) : BTreePage(page, page_id) {}
  pageid_t GetRootPageId() const;
  void SetRootPageId(pageid_t root_page_id);
  void Init();

 private:
  BTreeMetaPageData *GetSpecialData() const;
};

};  // namespace huadb