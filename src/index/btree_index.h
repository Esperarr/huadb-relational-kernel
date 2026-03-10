#pragma once

#include "index/btree_internal_page.h"
#include "index/btree_leaf_page.h"
#include "index/btree_page.h"
#include "index/index.h"
#include "log/log_manager.h"
#include "storage/buffer_pool.h"

namespace huadb {

class BTreePageState {
 public:
  db_size_t db_oid_;                        // 索引所在的数据库 oid
  db_size_t oid_;                           // 索引 oid
  BufferPool &buffer_pool_;                 // 页面缓冲池
  pageid_t page_id_;                        // 当前将要写入页面的id
  db_size_t level_;                         // 当前将要写入的b+树页面所在层次，leaf为0层
  std::shared_ptr<BTreePageState> parent_;  // 上一层的state，例如第0层的parent为第1层的state
  BTreePageState(db_size_t db_oid, db_size_t oid, BufferPool &buffer_pool, pageid_t page_id, db_size_t level)
      : db_oid_(db_oid), oid_(oid), buffer_pool_(buffer_pool), page_id_(page_id), level_(level), parent_(nullptr) {}

  std::shared_ptr<BTreePage> GetPage() const {
    if (level_ == 0) {
      return std::make_shared<BTreeLeafPage>(buffer_pool_.GetPage(db_oid_, oid_, page_id_), page_id_);
    } else {
      return std::make_shared<BTreeInternalPage>(buffer_pool_.GetPage(db_oid_, oid_, page_id_), page_id_);
    }
  }
};

// B+ 树的索引扫描区间
class Segment {
 public:
  std::vector<Value> left_val_;
  bool left_eq_;
  std::vector<Value> right_val_;
  bool right_eq_;

  Segment(std::vector<Value> left_val, bool left_eq, std::vector<Value> right_val, bool right_eq)
      : left_val_(std::move(left_val)), left_eq_(left_eq), right_val_(right_val), right_eq_(right_eq) {}

  // 你可以实现一些辅助函数
  // LAB 6 BEGIN
};

class BTreeIndex : public Index {
 public:
  BTreeIndex(BufferPool &buffer_pool, LogManager &log_manager, Disk &disk, oid_t oid, oid_t db_oid,
             std::shared_ptr<ColumnList> schema, std::shared_ptr<Table> table, bool new_index = true,
             pageid_t n_page = 0);
  Rid Search(const std::vector<Value> &val, const bool eq) const;
  void Build();
  // 导出b+树(子树)的.dot文件，可用GraphViz可视化，已经实现好，无需修改
  void Draw(std::string out_path = std::string("./tmp.dot"), pageid_t page_id = 0) override;

 private:
  void BuildAdd(std::shared_ptr<BTreePageState> state, std::shared_ptr<IndexRecord> index_record);
  void FinishBuild(std::shared_ptr<BTreePageState> state);
  void SetRootPageId(pageid_t root_page_id);
  pageid_t GetRootPageId() const;
  std::string ToGraph(pageid_t page_id);

  // 你可以实现一些辅助函数
  // LAB 6 BEGIN
};

}  // namespace huadb
