#pragma once

#include "catalog/column_list.h"
#include "storage/buffer_pool.h"

namespace huadb {

// 这里只能前向声明，不能引入头文件，否则会造成循环引用
// table.h<-index.h<-catalog.h<-log_manager.h<-table.h
class Table;

enum class IndexType { BTreeIndex, HashTableIndex };

class Index {
 public:
  virtual void Draw(std::string out_path = std::string("./tmp.dot"), pageid_t page_id = 0) = 0;
  Index(BufferPool &buffer_pool, LogManager &log_manager, Disk &disk, oid_t oid, oid_t db_oid,
        std::shared_ptr<ColumnList> schema, std::shared_ptr<Table> table, bool new_index, pageid_t n_page = 0)
      : buffer_pool_(buffer_pool),
        log_manager_(log_manager),
        disk_(disk),
        oid_(oid),
        db_oid_(db_oid),
        schema_(schema),
        table_(table),
        npage_(n_page) {}
  std::shared_ptr<ColumnList> GetColumns() const { return schema_; }
  std::vector<size_t> GetColumnIndices() const;
  oid_t GetTableOid() const;
  oid_t GetOid() const;
  oid_t GetDbOid() const;

 protected:
  BufferPool &buffer_pool_;
  LogManager &log_manager_;
  Disk &disk_;
  oid_t oid_;
  oid_t db_oid_;
  std::shared_ptr<ColumnList> schema_;
  std::shared_ptr<Table> table_;
  pageid_t npage_;  // 索引包含的页面数量
};

}  // namespace huadb
