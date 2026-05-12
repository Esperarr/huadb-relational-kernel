#pragma once

#include <limits>

#include "index/btree_internal_page.h"
#include "index/btree_leaf_page.h"
#include "index/btree_page.h"
#include "index/index.h"
#include "log/log_manager.h"
#include "storage/buffer_pool.h"

namespace huadb {

// 扩展键 (values_, heap_rid_) 中作为隐式末位列的两枚预置探针 rid。
// 当调用方只持有 user key、需要凑成完整扩展键参与比较时，可选用其中之一
// 作为探针的 heap_rid 部分：
//   MIN_RID = {0, 0}：作为探针时表现为"落在所有并列 key 段的最左侧"。
//   MAX_RID = {NULL_PAGE_ID, UINT16_MAX}：严格大于任何真实 heap_rid（NULL_PAGE_ID 为保留页号），总是落在所有并列 key
//   段的右端之后。
inline constexpr Rid MIN_RID{0, 0};
inline constexpr Rid MAX_RID{NULL_PAGE_ID, std::numeric_limits<slotid_t>::max()};

// 你可以实现一些辅助函数
// LAB 6 BEGIN

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
// 端点用 user-key 的"约束前缀"表示——只持有被实际约束的前缀列，未约束的尾列不出现。
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
  // 把当前 B+ 树的所有页面（meta / internal / leaf）以及堆表里所有行的 (rid, indexed_col_values)
  // 序列化为多行字符串，每行一个节点。供 SHOW <idx_name> 与 verify btree 测试指令使用。
  // 输出格式（每行第一个 token 即类型标签）：
  //   META root=<id|NULL>
  //   LEAF page=<id> prev=<id|NULL> next=<id|NULL> slots=<n> | <slot_0> | ... | <slot_{n-1}>
  //   INTERNAL page=<id> slots=<n> | <slot_0> | ... | <slot_{n-1}>
  //   HEAP rid=(<p>,<s>) values=(<v1>,<v2>,...)
  // 其中每个 slot 序列化为：values=(v1,v2,...);heap_rid=(p,s);ptr=(p,s)
  // 注：本函数为评测基础设施。grade 时会用测试仓库中的 canonical 实现覆盖学生提交的版本，
  //     学生不要修改这个文件 (btree_print.cpp)。
  std::string Print();

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
