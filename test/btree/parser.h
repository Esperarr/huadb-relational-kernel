// B+ 树文本 dump（SHOW <idx_name> 的输出）解析器。
//
// 此文件仅依赖标准库——不 #include 任何 src/index/ 或 src/common/ 头，
// 与 huadb 主库完全解耦。

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace huadb::btree_test {

// 占位"无效页号"。Print 输出 NULL 时由解析侧映射为该 sentinel。
constexpr int kNullPage = -1;

// (page_id, slot_id) — 既用于堆表 rid，也用于 leaf/internal 的 ptr 字段
using PageRid = std::pair<int, int>;

// IndexRecord 的解析视图（leaf 与 internal 共用）。
// values 直接保留为字符串，类型在比较时由 CompareValue 启发式恢复（int / 其它）。
struct Slot {
  std::vector<std::string> values;
  PageRid heap_rid;
  PageRid ptr;
};

struct LeafPage {
  int page_id;
  int prev_id;  // kNullPage 表示链头
  int next_id;  // kNullPage 表示链尾
  std::vector<Slot> slots;
};

struct InternalPage {
  int page_id;
  std::vector<Slot> slots;
};

struct HeapRow {
  PageRid rid;
  std::vector<std::string> values;
};

// SHOW <idx_name> 输出解析后的内存模型
struct PrintedTree {
  int root_id = kNullPage;         // META 行的 root；空树时为 kNullPage
  std::map<int, LeafPage> leaves;  // page_id → leaf
  std::map<int, InternalPage> internals;
  std::vector<HeapRow> heap;

  bool IsLeaf(int page_id) const { return leaves.count(page_id) > 0; }
  bool IsInternal(int page_id) const { return internals.count(page_id) > 0; }
  bool ExistsAsPage(int page_id) const { return IsLeaf(page_id) || IsInternal(page_id); }
};

// 解析失败时通过 ok=false + msg 返回；不抛异常，便于上层把诊断写到测试报告中。
struct ParseResult {
  bool ok = true;
  std::string msg;
  PrintedTree tree;
};

ParseResult ParsePrintOutput(const std::string &input);

}  // namespace huadb::btree_test
