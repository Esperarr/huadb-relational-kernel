#pragma once

#include <memory>

#include "common/types.h"
#include "storage/page.h"
#include "table/record.h"

namespace huadb {

enum class BTreePageType : uint8_t { INTERNAL = 0, LEAF = 1, META = 2 };

static constexpr db_size_t BTREE_PAGE_HEADER_SIZE = sizeof(lsn_t) + sizeof(BTreePageType) + sizeof(pageid_t);

/* B+树页面形式
 * +-------------+----------+----------+--------------------------+
 * |  page_lsn_  |  lower_  |  upper_  |  special_  |  page_type  |
 * +-------------+----------+----------+--------------------------+
 * |  slot[0]...slots[n]  |                                       |
 * +-------------+------------------------------------------------+
 *               ^ lower_ -->
 *
 *                   <-- v upper_
 * +--------------------------------------------------------------+
 * |                     |  index_record n,index_record n-1...    |
 * +---------------------+----------------------------------------+
 *
 *                       v special_ -->
 * +---------------------+----------------------------------------+
 * |  ...index_record 0  |  space remained for subclass           |
 * +---------------------+----------------------------------------+
 * 其中，page_lsn_,lower_,upper_,slots_,page_type_是所有b+树页面都具有的，因此放在最前面
 * space remained for subclass是为继承BtreePage的子类预留的空间
 * 可以通过sizeof(子类)-sizeof(父类)得到
 * 例如BTreeLeafPage还需要存储指向下一页的指针，而BTreeInternalPage不需要
 * BTreeMetaPage需要存储root_page_id_，但不需要存储任何元组，因此lower_和upper_均指向page_type_的后一个字节
 * 参考: postgresql-16.0/src/include/storage/bufpage.h
 */

class BTreePageData {
 public:
  BTreePageData() = delete;  // 这只是用于将char*的页面数据结构化的模板，不能实例化
  lsn_t page_lsn_;
  db_size_t lower_;          // 页面 lower 偏移
  db_size_t upper_;          // 页面 upper 偏移
  db_size_t special_;        // 页面 special 偏移
  BTreePageType page_type_;  // 页面类型
  Slot slots_[];             // 槽位数组指针，本身不占空间
};

class BTreePage {
 public:
  explicit BTreePage(std::shared_ptr<Page> page, pageid_t page_id)
      : page_(page), data_(reinterpret_cast<BTreePageData *>(page->GetData())), page_id_(page_id) {}
  void Init();
  bool IsLeafPage() const;
  void SetPageLSN(lsn_t lsn);
  void SetPageType(BTreePageType page_type);
  db_size_t GetFreeSpaceSize() const;
  db_size_t GetRecordCount() const;
  slotid_t AddRecord(std::shared_ptr<IndexRecord> record, slotid_t slot_id = INVALID_SLOT);
  std::shared_ptr<IndexRecord> GetRecord(slotid_t slot_id, ColumnList schema);
  char *GetData() const;
  pageid_t GetPageId() const;

 protected:
  void *GetSpecialData() const;
  std::shared_ptr<Page> page_;
  BTreePageData *data_;
  pageid_t page_id_;
};

}  // namespace huadb
