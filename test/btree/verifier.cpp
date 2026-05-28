// B+ 树不变式验证器实现。
//
// 文件中实现 6 条不变式：
//   1) 拓扑结构是树
//   2) 叶子节点形成双向链表
//   3) 同一层内 slots 严格升序（按扩展键 (values, heap_rid)）
//   4) 父 slot.key == 所指向子页 slot[0].key（key=min 不变式）
//   5) 树高与期望值一致
//   6) 叶子层 (values, heap_rid) 集合 == 堆表所有行的 (indexed_values, rid) 集合
//      且每个叶子页面满足：slot.ptr == slot.heap_rid
//
// 添加一条新不变式只要在 GetInvariants() 列表末尾追加 {id, name, &CheckXxx}。

#include "verifier.h"

#include <algorithm>
#include <set>

namespace huadb::btree_test {

namespace {

// =========================================================================
// 通用比较辅助
// =========================================================================

// 启发式判定字符串看起来像不像一个 int 字面量（含可选负号）。
// 用于 lab 数据中区分 int / varchar 列。
bool LooksLikeInt(const std::string& s) {
  if (s.empty()) return false;
  size_t i = (s[0] == '-') ? 1 : 0;
  if (i == s.size()) return false;  // 仅一个 '-'
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

int CompareValue(const std::string& a, const std::string& b) {
  if (LooksLikeInt(a) && LooksLikeInt(b)) {
    long long la = std::stoll(a);
    long long lb = std::stoll(b);
    return (la < lb) ? -1 : (la > lb ? 1 : 0);
  }
  return a < b ? -1 : (a > b ? 1 : 0);
}

int CompareValues(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    int c = CompareValue(a[i], b[i]);
    if (c) return c;
  }
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  return 0;
}

int CompareRids(PageRid a, PageRid b) {
  if (a.first != b.first) return a.first < b.first ? -1 : 1;
  if (a.second != b.second) return a.second < b.second ? -1 : 1;
  return 0;
}

int CompareExtendedKey(const std::vector<std::string>& va, PageRid ra,
                        const std::vector<std::string>& vb, PageRid rb) {
  int c = CompareValues(va, vb);
  if (c) return c;
  return CompareRids(ra, rb);
}

std::string FormatRid(PageRid r) {
  return "(" + std::to_string(r.first) + "," + std::to_string(r.second) + ")";
}

std::string FormatValues(const std::vector<std::string>& v) {
  std::string out = "(";
  for (size_t i = 0; i < v.size(); ++i) {
    if (i > 0) out += ",";
    out += v[i];
  }
  return out + ")";
}

std::string FormatKey(const std::vector<std::string>& v, PageRid r) {
  return FormatValues(v) + "+" + FormatRid(r);
}

const std::vector<Slot>* SlotsOf(const PrintedTree& t, int page_id) {
  auto it_l = t.leaves.find(page_id);
  if (it_l != t.leaves.end()) return &it_l->second.slots;
  auto it_i = t.internals.find(page_id);
  if (it_i != t.internals.end()) return &it_i->second.slots;
  return nullptr;
}

// =========================================================================
// Invariant 1: 拓扑结构是树
// =========================================================================
// 子项：
// (a) root_id == NULL 时，索引中不应有任何 LEAF / INTERNAL（即只有 meta page）
// (b) root_id 必须指向一个存在的 LEAF / INTERNAL 页
// (c) 从 root BFS：每个 internal slot.ptr.slot_id 必须为 0；ptr.page_id 必须存在；
//     不能形成环；每个非 root 页必须恰被一个 internal slot 引用
// (d) 从 root 可达的页面集合 == 全部 LEAF ∪ INTERNAL（无孤儿）
// (e) 每个 internal page 的所有子页类型必须一致（要么全 leaf 要么全 internal）
// (f) 全树深度均衡：所有 leaf 必须处于同一深度（root 深度=0）。
//     局部 same-type-children 不能递归推出全树平衡——例如 root 的两子都是
//     internal，但其中一个的子是 leaf、另一个的子是 internal，仍然通过 (e)。
CheckResult CheckTopology(const PrintedTree& t, int /*expected_height*/) {
  bool any_pages = !t.leaves.empty() || !t.internals.empty();
  if (t.root_id == kNullPage) {
    if (any_pages) {
      return {false, "root=NULL but " + std::to_string(t.leaves.size() + t.internals.size()) +
                          " non-meta page(s) exist (FinishBuild's SetRootPageId not called?)"};
    }
    return {true, ""};
  }
  if (!t.ExistsAsPage(t.root_id)) {
    return {false, "root=" + std::to_string(t.root_id) + " not found among LEAF/INTERNAL pages"};
  }

  // BFS
  std::map<int, int> parent;
  std::map<int, int> depth;
  depth[t.root_id] = 0;
  std::vector<int> queue;
  queue.push_back(t.root_id);
  size_t head = 0;
  while (head < queue.size()) {
    int pid = queue[head++];
    if (t.IsLeaf(pid)) continue;
    auto it = t.internals.find(pid);
    if (it == t.internals.end()) continue;  // shouldn't happen since visited
    const auto& page = it->second;
    for (size_t i = 0; i < page.slots.size(); ++i) {
      const auto& s = page.slots[i];
      if (s.ptr.second != 0) {
        return {false, "internal page " + std::to_string(pid) + " slot " + std::to_string(i) +
                            " ptr.slot_id=" + std::to_string(s.ptr.second) + ", expected 0"};
      }
      int child = s.ptr.first;
      if (!t.ExistsAsPage(child)) {
        return {false, "internal page " + std::to_string(pid) + " slot " + std::to_string(i) +
                            " ptr.page_id=" + std::to_string(child) + " not found among pages"};
      }
      if (child == t.root_id) {
        return {false, "internal page " + std::to_string(pid) + " slot " + std::to_string(i) +
                            " refers to root page " + std::to_string(child) + " (cycle through root)"};
      }
      auto par_it = parent.find(child);
      if (par_it != parent.end()) {
        return {false, "page " + std::to_string(child) + " referenced as child by both page " +
                            std::to_string(par_it->second) + " and page " + std::to_string(pid)};
      }
      parent[child] = pid;
      depth[child] = depth[pid] + 1;
      queue.push_back(child);
    }
  }

  size_t total_pages = t.leaves.size() + t.internals.size();
  if (queue.size() != total_pages) {
    for (const auto& kv : t.leaves) {
      if (kv.first != t.root_id && parent.find(kv.first) == parent.end()) {
        return {false, "leaf page " + std::to_string(kv.first) + " unreachable from root"};
      }
    }
    for (const auto& kv : t.internals) {
      if (kv.first != t.root_id && parent.find(kv.first) == parent.end()) {
        return {false, "internal page " + std::to_string(kv.first) + " unreachable from root"};
      }
    }
    return {false, "some pages unreachable from root (count mismatch)"};
  }

  // 每个 internal page 的子页类型必须统一
  for (const auto& kv : t.internals) {
    const auto& page = kv.second;
    if (page.slots.empty()) continue;
    int first_child = page.slots[0].ptr.first;
    bool first_is_leaf = t.IsLeaf(first_child);
    for (size_t i = 1; i < page.slots.size(); ++i) {
      int c = page.slots[i].ptr.first;
      if (t.IsLeaf(c) != first_is_leaf) {
        return {false, "internal page " + std::to_string(kv.first) +
                            " has mixed-type children (slot 0 → " +
                            (first_is_leaf ? "leaf" : "internal") + " " + std::to_string(first_child) +
                            ", slot " + std::to_string(i) + " → " +
                            (t.IsLeaf(c) ? "leaf" : "internal") + " " + std::to_string(c) + ")"};
      }
    }
  }

  // 全树深度均衡：所有 leaf 处于同一深度。
  // 不可达的 leaf 已被前面的 count-mismatch 分支拦下，到这里所有 leaf 都在 depth 表里。
  int min_depth = -1, max_depth = -1;
  int min_leaf = -1, max_leaf = -1;
  for (const auto& kv : t.leaves) {
    auto d_it = depth.find(kv.first);
    if (d_it == depth.end()) continue;
    int d = d_it->second;
    if (min_depth < 0 || d < min_depth) { min_depth = d; min_leaf = kv.first; }
    if (max_depth < 0 || d > max_depth) { max_depth = d; max_leaf = kv.first; }
  }
  if (min_depth >= 0 && min_depth != max_depth) {
    return {false, "leaf depth not uniform: leaf " + std::to_string(min_leaf) +
                        " at depth " + std::to_string(min_depth) +
                        ", leaf " + std::to_string(max_leaf) +
                        " at depth " + std::to_string(max_depth)};
  }

  return {true, ""};
}

// =========================================================================
// Invariant 2: 叶子双向链表
// =========================================================================
// (a) 从 root 一路下钻到 slot[0] 所指向的最左叶
// (b) 沿 next 走完整条链 ⇒ 链上的 leaf 集合 == 拓扑遍历中所有 leaf 的集合
// (c) 第一个 leaf prev=NULL，最后一个 next=NULL（后者由 while 终止条件保证）
// (d) 每个非首位 leaf 的 prev 必须等于它在 forward 链中的前驱
CheckResult CheckLeafChain(const PrintedTree& t, int /*expected_height*/) {
  if (t.root_id == kNullPage) return {true, ""};
  if (t.leaves.empty()) {
    return {false, "no LEAF pages but root=" + std::to_string(t.root_id) + " is set"};
  }

  int cur = t.root_id;
  while (t.IsInternal(cur)) {
    const auto& p = t.internals.at(cur);
    if (p.slots.empty()) {
      return {false, "internal page " + std::to_string(cur) + " has 0 slots, can't descend to leftmost leaf"};
    }
    cur = p.slots[0].ptr.first;
    if (!t.ExistsAsPage(cur)) {
      return {false, "leftmost descent stuck: child " + std::to_string(cur) + " of an internal page not found"};
    }
  }

  std::vector<int> forward;
  std::set<int> visited;
  while (cur != kNullPage) {
    if (!t.IsLeaf(cur)) {
      return {false, "next chain leaves leaf level: page " + std::to_string(cur) + " is not a leaf"};
    }
    if (visited.count(cur)) {
      return {false, "leaf next chain has cycle through page " + std::to_string(cur)};
    }
    visited.insert(cur);
    forward.push_back(cur);
    cur = t.leaves.at(cur).next_id;
  }

  if (t.leaves.at(forward.front()).prev_id != kNullPage) {
    return {false, "leftmost leaf " + std::to_string(forward.front()) +
                        " has prev=" + std::to_string(t.leaves.at(forward.front()).prev_id) +
                        ", expected NULL"};
  }
  if (forward.size() != t.leaves.size()) {
    return {false, "forward chain visits " + std::to_string(forward.size()) +
                        " leaves but tree has " + std::to_string(t.leaves.size())};
  }
  for (size_t i = 1; i < forward.size(); ++i) {
    int expected_prev = forward[i - 1];
    int actual_prev = t.leaves.at(forward[i]).prev_id;
    if (actual_prev != expected_prev) {
      return {false, "leaf " + std::to_string(forward[i]) +
                          " prev=" + std::to_string(actual_prev) +
                          " but predecessor in next-chain is " + std::to_string(expected_prev)};
    }
  }
  return {true, ""};
}

// =========================================================================
// Invariant 3: 层内严格升序（扩展键）
// =========================================================================
// (a) 同一页内：slot[i].extended_key < slot[i+1].extended_key
// (b) 叶子层跨页：相邻 leaf（按 next 链）的 last_slot < first_slot
// internal 层的跨页升序由 (a) + invariant 4 隐式保证
CheckResult CheckWithinLevelAscending(const PrintedTree& t, int /*expected_height*/) {
  auto check_within_page = [&](int pid, const std::vector<Slot>& slots, const char* type) -> CheckResult {
    for (size_t i = 1; i < slots.size(); ++i) {
      int c = CompareExtendedKey(slots[i - 1].values, slots[i - 1].heap_rid,
                                  slots[i].values, slots[i].heap_rid);
      if (c >= 0) {
        return {false, std::string(type) + " page " + std::to_string(pid) +
                            " slot " + std::to_string(i) + " extended_key " +
                            FormatKey(slots[i].values, slots[i].heap_rid) +
                            " not strictly greater than slot " + std::to_string(i - 1) + " " +
                            FormatKey(slots[i - 1].values, slots[i - 1].heap_rid)};
      }
    }
    return {true, ""};
  };
  for (const auto& kv : t.leaves) {
    auto r = check_within_page(kv.first, kv.second.slots, "leaf");
    if (!r.ok) return r;
  }
  for (const auto& kv : t.internals) {
    auto r = check_within_page(kv.first, kv.second.slots, "internal");
    if (!r.ok) return r;
  }

  // 跨叶子页升序
  if (t.root_id == kNullPage || t.leaves.empty()) return {true, ""};
  int cur = t.root_id;
  while (t.IsInternal(cur)) {
    const auto& p = t.internals.at(cur);
    if (p.slots.empty()) return {true, ""};
    cur = p.slots[0].ptr.first;
    if (!t.ExistsAsPage(cur)) return {true, ""};  // 由 inv 1 抓
  }
  int prev_leaf = kNullPage;
  while (cur != kNullPage) {
    const auto& leaf = t.leaves.at(cur);
    if (prev_leaf != kNullPage) {
      const auto& prev = t.leaves.at(prev_leaf);
      if (!prev.slots.empty() && !leaf.slots.empty()) {
        const auto& last = prev.slots.back();
        const auto& first = leaf.slots.front();
        int c = CompareExtendedKey(last.values, last.heap_rid, first.values, first.heap_rid);
        if (c >= 0) {
          return {false, "cross-leaf ordering broken: leaf " + std::to_string(prev_leaf) +
                              " last slot " + FormatKey(last.values, last.heap_rid) +
                              " not strictly less than leaf " + std::to_string(cur) +
                              " first slot " + FormatKey(first.values, first.heap_rid)};
        }
      }
    }
    prev_leaf = cur;
    cur = leaf.next_id;
  }
  return {true, ""};
}

// =========================================================================
// Invariant 4: 父 slot.key == 子 slot[0].key
// =========================================================================
CheckResult CheckParentKeyEqualsChildMin(const PrintedTree& t, int /*expected_height*/) {
  for (const auto& kv : t.internals) {
    int pid = kv.first;
    const auto& page = kv.second;
    for (size_t i = 0; i < page.slots.size(); ++i) {
      const auto& slot = page.slots[i];
      int child_id = slot.ptr.first;
      const auto* child_slots = SlotsOf(t, child_id);
      if (!child_slots) {
        return {false, "internal page " + std::to_string(pid) + " slot " + std::to_string(i) +
                            " ptr=" + std::to_string(child_id) +
                            " not found (should have been caught by invariant 1)"};
      }
      if (child_slots->empty()) {
        return {false, "internal page " + std::to_string(pid) + " slot " + std::to_string(i) +
                            " child page " + std::to_string(child_id) +
                            " has 0 slots, can't compute child[0] key"};
      }
      int c = CompareExtendedKey(slot.values, slot.heap_rid,
                                  (*child_slots)[0].values, (*child_slots)[0].heap_rid);
      if (c != 0) {
        return {false, "internal page " + std::to_string(pid) + " slot " + std::to_string(i) +
                            " key=" + FormatKey(slot.values, slot.heap_rid) +
                            " but child page " + std::to_string(child_id) +
                            " slot[0] key=" + FormatKey((*child_slots)[0].values, (*child_slots)[0].heap_rid)};
      }
    }
  }
  return {true, ""};
}

// =========================================================================
// Invariant 5: 树高
// =========================================================================
// 约定：单 leaf 即 root 时高度 = 1；多页树每多一层 internal 高度 +1。
// 空树（root=NULL）高度 = 0。
CheckResult CheckHeight(const PrintedTree& t, int expected_height) {
  if (t.root_id == kNullPage) {
    if (expected_height != 0) {
      return {false, "tree is empty (root=NULL) but expected_height=" + std::to_string(expected_height)};
    }
    return {true, ""};
  }
  int cur = t.root_id;
  int height = 1;
  while (t.IsInternal(cur)) {
    const auto& p = t.internals.at(cur);
    if (p.slots.empty()) {
      return {false, "internal page " + std::to_string(cur) + " has 0 slots, height undefined"};
    }
    cur = p.slots[0].ptr.first;
    if (!t.ExistsAsPage(cur)) {
      return {false, "descent stuck during height measurement: child " + std::to_string(cur) + " not found"};
    }
    ++height;
  }
  if (height != expected_height) {
    return {false, "tree height = " + std::to_string(height) +
                        ", expected " + std::to_string(expected_height)};
  }
  return {true, ""};
}

// =========================================================================
// Invariant 6: 叶子记录 ↔ 堆表元组一一对应 + leaf 约定 ptr == heap_rid
// =========================================================================
CheckResult CheckHeapMatch(const PrintedTree& t, int /*expected_height*/) {
  using Item = std::pair<std::vector<std::string>, PageRid>;
  std::vector<Item> leaf_set;
  for (const auto& kv : t.leaves) {
    int pid = kv.first;
    const auto& page = kv.second;
    for (size_t i = 0; i < page.slots.size(); ++i) {
      const auto& s = page.slots[i];
      if (s.ptr != s.heap_rid) {
        return {false, "leaf page " + std::to_string(pid) + " slot " + std::to_string(i) +
                            " ptr=" + FormatRid(s.ptr) +
                            " != heap_rid=" + FormatRid(s.heap_rid) +
                            " (leaf convention violated)"};
      }
      leaf_set.emplace_back(s.values, s.heap_rid);
    }
  }
  std::vector<Item> heap_set;
  for (const auto& h : t.heap) heap_set.emplace_back(h.values, h.rid);

  auto less = [](const Item& a, const Item& b) {
    int c = CompareValues(a.first, b.first);
    if (c) return c < 0;
    return CompareRids(a.second, b.second) < 0;
  };
  std::sort(leaf_set.begin(), leaf_set.end(), less);
  std::sort(heap_set.begin(), heap_set.end(), less);

  if (leaf_set.size() != heap_set.size()) {
    return {false, "leaf record count = " + std::to_string(leaf_set.size()) +
                        " but heap row count = " + std::to_string(heap_set.size())};
  }
  for (size_t i = 0; i < leaf_set.size(); ++i) {
    if (leaf_set[i] != heap_set[i]) {
      return {false, "first mismatch at sorted position " + std::to_string(i) +
                          ": leaf has " + FormatKey(leaf_set[i].first, leaf_set[i].second) +
                          " vs heap has " + FormatKey(heap_set[i].first, heap_set[i].second)};
    }
  }
  return {true, ""};
}

}  // namespace

const std::vector<Invariant>& GetInvariants() {
  // 添加新的 invariant 在这里追加一项即可。
  static const std::vector<Invariant> kInvariants = {
    {1, "topology", &CheckTopology},
    {2, "leaf_chain", &CheckLeafChain},
    {3, "within_level_ascending", &CheckWithinLevelAscending},
    {4, "parent_key_equals_child_min", &CheckParentKeyEqualsChildMin},
    {5, "height", &CheckHeight},
    {6, "heap_match", &CheckHeapMatch},
  };
  return kInvariants;
}

std::string Verify(const PrintedTree& tree, int expected_height) {
  for (const auto& inv : GetInvariants()) {
    auto r = inv.check(tree, expected_height);
    if (!r.ok) {
      return "FAIL: invariant " + std::to_string(inv.id) + ": " + r.msg;
    }
  }
  return "OK";
}

}  // namespace huadb::btree_test
