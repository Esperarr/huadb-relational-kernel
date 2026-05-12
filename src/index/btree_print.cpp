// 评测基础设施：B+ 树文本 dump。
//
// 本文件实现 BTreeIndex::Print()，将索引的当前内存形态以行式字符串导出，
// 供 SHOW <idx_name> SQL 命令与测试驱动的 verify btree 指令消费。
//
// 注意（grade 流程）：
//   评测时此文件会被测试仓库中的 canonical 副本无条件覆盖。学生本地可以
//   编辑或调试，但任何改动在 grade 时不会生效。请不要在自己的实现中绕开
//   或自定义 Print 的输出格式，否则验证器会无法解析。

#include <sstream>

#include "common/constants.h"
#include "index/btree_index.h"
#include "index/btree_internal_page.h"
#include "index/btree_leaf_page.h"
#include "index/btree_meta_page.h"
#include "table/record.h"
#include "table/table.h"
#include "table/table_scan.h"

namespace huadb {

namespace {

inline std::string PageIdOrNull(pageid_t pid) {
  return pid == NULL_PAGE_ID ? std::string("NULL") : std::to_string(pid);
}

inline std::string FormatRid(const Rid &rid) {
  return "(" + std::to_string(rid.page_id_) + "," + std::to_string(rid.slot_id_) + ")";
}

std::string FormatValues(const std::vector<Value> &values) {
  std::string out = "(";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) out += ",";
    out += values[i].ToString();
  }
  out += ")";
  return out;
}

std::string FormatSlot(const std::shared_ptr<IndexRecord> &rec) {
  std::string out;
  out += "values=";
  out += FormatValues(rec->GetValues());
  out += ";heap_rid=";
  out += FormatRid(rec->GetHeapRid());
  out += ";ptr=";
  out += FormatRid(rec->GetPtr());
  return out;
}

}  // namespace

std::string BTreeIndex::Print() {
  std::ostringstream out;

  // 1) META 行
  auto meta_page = std::make_shared<BTreeMetaPage>(buffer_pool_.GetPage(db_oid_, oid_, 0), 0);
  pageid_t root_id = meta_page->GetRootPageId();
  out << "META root=" << PageIdOrNull(root_id) << "\n";

  // 2) 遍历所有非 meta 页（page_id ∈ [1, npage_-1]），按 page_id 升序输出
  // 顺序无意义；验证器以 page_id 自行重建拓扑。空树（root=NULL）下，
  // 通常仍存在 1 个空 leaf 页（Build 在表空时也会先分配第一个 leaf），
  // 这里照样会输出对应的 LEAF 行。
  for (pageid_t pid = 1; pid < npage_; ++pid) {
    auto raw = std::make_shared<BTreePage>(buffer_pool_.GetPage(db_oid_, oid_, pid), pid);
    db_size_t slot_count = raw->GetRecordCount();

    if (raw->IsLeafPage()) {
      auto leaf = std::make_shared<BTreeLeafPage>(buffer_pool_.GetPage(db_oid_, oid_, pid), pid);
      out << "LEAF page=" << pid << " prev=" << PageIdOrNull(leaf->GetPrevPageId())
          << " next=" << PageIdOrNull(leaf->GetNextPageId()) << " slots=" << slot_count;
    } else {
      out << "INTERNAL page=" << pid << " slots=" << slot_count;
    }

    for (slotid_t s = 0; s < slot_count; ++s) {
      auto rec = raw->GetRecord(s, *schema_);
      out << " | " << FormatSlot(rec);
    }
    out << "\n";
  }

  // 3) HEAP 段：把堆表所有元组的 (rid, indexed_col_values) 全部 dump 出来。
  // 验证器据此核对叶子层和堆表的一一对应（不变式 6）。
  // 列投影规则与 Build() 中 key_indices 的算法保持一致。
  const auto &table_schema = table_->GetColumnList();
  std::vector<size_t> key_indices;
  for (const auto &column : schema_->GetColumns()) {
    key_indices.emplace_back(table_schema.GetColumnIndex(column.GetName()));
  }

  auto scan = std::make_shared<TableScan>(buffer_pool_, table_, Rid{table_->GetFirstPageId(), 0});
  while (auto record = scan->GetNextRecord()) {
    std::vector<Value> projected;
    projected.reserve(key_indices.size());
    for (size_t idx : key_indices) {
      projected.emplace_back(record->GetValue(idx));
    }
    out << "HEAP rid=" << FormatRid(record->GetRid()) << " values=" << FormatValues(projected) << "\n";
  }

  return out.str();
}

}  // namespace huadb
