#pragma once

#include "btree_page.h"
#include "table/record.h"

namespace huadb {

class BTreeLeafPageData {
 public:
  BTreeLeafPageData() = delete;
  pageid_t prev_page_id_;
  pageid_t next_page_id_;
};

class BTreeLeafPage : public BTreePage {
 public:
  explicit BTreeLeafPage(std::shared_ptr<Page> page, pageid_t page_id) : BTreePage(page, page_id){};
  void Init();
  void Insert(std::shared_ptr<Record> record);
  void Delete();
  pageid_t GetPrevPageId() const;
  void SetPrevPageId(pageid_t prev_pageid);
  pageid_t GetNextPageId() const;
  void SetNextPageId(pageid_t next_pageid);

 private:
  BTreeLeafPageData *GetSpecialData() const;
};

}  // namespace huadb
