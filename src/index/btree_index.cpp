#include "index/btree_index.h"

#include <algorithm>
#include <iostream>

#include "common/constants.h"
#include "index/btree_internal_page.h"
#include "index/btree_leaf_page.h"
#include "index/btree_meta_page.h"
#include "table/table.h"
#include "table/table_page.h"
#include "table/table_scan.h"

namespace huadb {

BTreeIndex::BTreeIndex(BufferPool &buffer_pool, LogManager &log_manager, Disk &disk, oid_t oid, oid_t db_oid,
                       std::shared_ptr<ColumnList> schema, std::shared_ptr<Table> table, bool new_index, pageid_t npage)
    : Index(buffer_pool, log_manager, disk, oid, db_oid, schema, table, npage) {
  if (new_index) {
    // 创建B+树的meta page，page_id为0，并初始化，需要将npage_加1
    auto meta_page = std::make_unique<BTreeMetaPage>(buffer_pool_.NewPage(db_oid_, oid_, 0), 0);
    meta_page->Init();
    ++npage_;
  }
}

void BTreeIndex::Build() {
  // Step1. 得到索引key的schema在table中的对应的列id
  auto table_schema = table_->GetColumnList();
  std::vector<size_t> key_indices;
  for (auto column : schema_->GetColumns()) {
    key_indices.emplace_back(table_schema.GetColumnIndex(column.GetName()));
  }

  // Step2. 将table中的所有元组根据(key,heap_rid)从小到大排序
  // 取出堆表的所有元组，将其构造为索引元组，指向相应的堆表元组
  // 为降低难度，采用内存排序即可
  auto index_records = std::vector<std::shared_ptr<IndexRecord>>();
  // LAB 6 BEGIN

  // Step3. 遍历排序好的元组，将每个元组插入b+树的叶子节点
  // 分配索引的第一个leaf
  // Page，page_id为npage_，并初始化，之后每次分配新页面时page_id都为npage_，分配完后记得将npage_加1
  // 记得初始化页面时要调用Init()，Init()会将页面的lower_和upper_设置好，保证页面一开始是空的
  // 记得创建页面对应的state，state中需要记录页面的page_id和level，leaf页面的level为0，internal页面的level为下层页面的level+1，state还需要记录父页面的state指针，初始为nullptr
  auto page = std::make_shared<BTreeLeafPage>(buffer_pool_.NewPage(db_oid_, oid_, npage_), npage_);
  page->Init();
  auto state = std::make_shared<BTreePageState>(db_oid_, oid_, buffer_pool_, npage_, 0);
  ++npage_;

  // 将排序好的索引元组逐个插入b+树，插入时要调用BuildAdd()，并传入当前页面的state，BuildAdd()会递归地将索引元组插入b+树，并在需要时分裂页面和创建上层页面
  for (auto index_record : index_records) {
    // LAB 6 BEGIN
  }

  // Step4. 到此为止，b+树每层最右边节点还没有设置父节点指向它的指针
  // 此外，还要根据最终b+树形态设置root_page_id_
  // 调用 FinishBuild() 来完成上述工作
  // LAB 6 BEGIN
}

void BTreeIndex::BuildAdd(std::shared_ptr<BTreePageState> state, std::shared_ptr<IndexRecord> index_record) {
  // Step1. 判断当前页面的空间是否足够，如果不够就要新加入页面，并设置上层页面到下层页面的指针
  if (state->GetPage()->GetFreeSpaceSize() < index_record->GetSize()) {
    // Step1.1. 设置上一层页面到原来页面的指针
    // 如果还没有创建上层页面，则递归调用BuildAdd()创建上层页面，根据state->parent_是否为nullptr判断是否存在上层页面
    if (state->parent_ == nullptr) {
      // 分配上层页面的空间，并初始化上层对应的state，设置state->parent_为上层state
      // LAB 6 BEGIN
    }

    // Step1.2. 构造父节点索引元组：key 为当前页面 slot[0] 的扩展键 (values, heap_rid)，
    // ptr 改指向当前页面。internal page 的 ptr_.slot_id_ 字段没有用，设置为 0 即可。
    //
    // 要求:  key_{i-1}     key_i
    //       page_{i-1}    page_i
    // 在 scheme C 下，扩展键 = (values, heap_rid)，heap_rid 作为隐式末位列让扩展键严格全序。
    // 需要满足：
    // (1) page_{i-1} 中所有记录的扩展键 < key_i 的扩展键
    // (2) key_i 的扩展键 == page_i 中最小记录的扩展键（即 page_i slot[0] 的扩展键）
    // 因扩展键严格唯一，不再有"两边可以取等号"的情形；每个子树的最小扩展键
    // 就是该子树在父节点里对应 slot 的扩展键，路由无歧义。
    //
    // 实现上：直接复制当前页面 slot[0] 的 IndexRecord（values 和 heap_rid 都已就位），
    // 只需把 ptr 改指向当前页即可——heap_rid 沿用 slot[0] 的值，这正是子树最小 heap_rid，
    // 并随着层层上升，该不变式自然保持（上层 slot[0] 的 heap_rid 仍是整棵子树最小）。
    auto parent_index_record = state->GetPage()->GetRecord(0, *schema_);
    parent_index_record->SetPtr({state->page_id_, 0});
    // 递归调用BuildAdd()将索引元组加入到上层页面
    // LAB 6 BEGIN

    // Step1.3. 分配新页面，更新当前层的state
    // 中间层：新建BTreeInternalPage，叶子层（level=0）：新建BTreeLeafPage
    // 如果为leaf页面需要设置连接关系
    auto old_page = state->GetPage();
    if (state->level_ == 0) {
      // LAB 6 BEGIN
    } else {
      // LAB 6 BEGIN
    }
  }

  // Step2. 插入索引元组，插入后别忘了设置index_record的rid_
  // LAB 6 BEGIN
}

void BTreeIndex::FinishBuild(std::shared_ptr<BTreePageState> state) {
  // Step1. 自底向上(思考为什么不是自顶向下)，将b+树最右侧节点的父->子指针设置好
  while (state->parent_) {
    auto page = state->GetPage();
    auto parent_state = state->parent_;
    auto parent_page = parent_state->GetPage();

    // Step1.1. 构造索引元组，同BuildAdd()
    // LAB 6 BEGIN

    // Step1.2. 调用BuildAdd()将索引元组加入到上层页面
    // LAB 6 BEGIN

    // Step1.3. 更新当前层的state
    // LAB 6 BEGIN
  }

  // Step2. 当前state对应的页面为root，将其写入b+树的metapage字段中
  // LAB 6 BEGIN
}

void BTreeIndex::SetRootPageId(pageid_t root_page_id) {
  // LAB 6 BEGIN
}

pageid_t BTreeIndex::GetRootPageId() const {
  // LAB 6 BEGIN
}

// 你可以实现一些辅助函数
// LAB 6 BEGIN

Rid BTreeIndex::Search(const std::vector<Value> &val, const bool eq) const {
  // 返回**第一个**满足条件的索引项
  // eq = true  : 返回第一个 key >= val 的索引项
  // eq = false : 返回第一个 key >  val 的索引项
  // 若不存在则返回无效的 rid: {NULL_PAGE_ID, 0}
  // LAB 6 BEGIN
}

void BTreeIndex::Draw(std::string out_path, pageid_t page_id) {
  std::string sfix = ".dot";
  if (out_path.substr(out_path.length() - sfix.length()) != sfix) {
    out_path += sfix;
  }
  std::string out = "";
  out += "digraph G {\n";
  out += "graph [splines=false]\n";
  out += "  node [shape=record];\n";
  if (page_id == 0) {
    out += "  0 [label=\"<metapage>metapage\\npage 0\",color=lightblue]\n";
    page_id = GetRootPageId();
    if (page_id == NULL_PAGE_ID) {
      out += "}";
      auto f = std::ofstream(out_path);
      f << out;
      return;
    }
    out += "  0:metapage:s->" + std::to_string(page_id) + ":n\n";
  }
  out += ToGraph(page_id);
  auto scan = std::make_shared<TableScan>(buffer_pool_, table_, Rid{table_->GetFirstPageId(), 0});
  out += "  table [label=\"{table|";
  for (auto record = scan->GetNextRecord(); record; record = scan->GetNextRecord()) {
    auto label = std::to_string(record->GetRid().page_id_) + "_" + std::to_string(record->GetRid().slot_id_);
    out += "<_" + label + ">" + label + ": (" + record->ToString() + ")|";
  }
  out.pop_back();
  out += "}\"]\n";
  out += "}";
  auto f = std::ofstream(out_path);
  f << out;
}

std::string BTreeIndex::ToGraph(pageid_t page_id) {
  std::string out = "";
  auto page = std::make_shared<BTreePage>(buffer_pool_.GetPage(db_oid_, oid_, page_id), page_id);
  auto node_id = std::to_string(page_id);
  out += std::string("  ") + node_id + " [label=\"page\\n" + node_id + "|";
  std::vector<std::string> labels;
  std::vector<Rid> ptrs;
  for (slotid_t i = 0; i < page->GetRecordCount(); ++i) {
    auto index_record = page->GetRecord(i, *schema_);
    index_record->SetRid({page_id, i});
    std::string label = std::string("_") + std::to_string(index_record->GetRid().page_id_) + "_" +
                        std::to_string(index_record->GetRid().slot_id_);
    std::string value = index_record->Record::ToString();
    out += "<" + label + ">" + value;
    if (page->IsLeafPage()) {
      out += "\\n" + std::to_string(index_record->GetPtr().page_id_) + "_" +
             std::to_string(index_record->GetPtr().slot_id_);
    }
    out += "|";
    labels.emplace_back(label);
    ptrs.emplace_back(index_record->GetPtr());
  }
  out.pop_back();
  out += "\"]\n";
  for (slotid_t i = 0; i < page->GetRecordCount(); ++i) {
    auto label = labels[i];
    auto ptr = ptrs[i];
    if (page->IsLeafPage()) {
      // auto table_page = std::make_shared<TablePage>(buffer_pool_.GetPage(db_oid_, table_->GetOid(), ptr.page_id_));
      // out += std::string("  ") + node_id + ":" + label + ":s->table:_" +
      // 			 std::to_string(ptr.page_id_) + "_" + std::to_string(ptr.slot_id_) + ":n\n";
    } else {
      out += ToGraph(ptr.page_id_);
      out += std::string("  ") + node_id + ":" + label + ":s->" + std::to_string(ptr.page_id_) + ":n\n";
    }
  }
  // if (page->IsLeafPage()) {
  // 	auto next_page_id = std::reinterpret_pointer_cast<BTreeLeafPage>(page)->GetNextPageId();
  // 	if (next_page_id != NULL_PAGE_ID) {
  // 		out += std::string("  ") + std::to_string(page_id) + ":e->" + std::to_string(next_page_id) + ":w\n";
  // 	}
  // }
  return out;
}

}  // namespace huadb
