// 验证器自测：用人造的 Print 字符串覆盖 6 条不变式的"通过"和"故意制造的失败"。
// 不依赖 huadb 引擎，独立可执行，作为 verifier 自身正确性的回归网。
//
// 运行：build 后 cd build/debug && ./bin/btree_verifier_test
// 输出：每条用例打 PASS/FAIL，最末行汇总。退出码 = 失败用例数。

#include <iostream>
#include <string>

#include "parser.h"
#include "verifier.h"

using namespace huadb::btree_test;

namespace {

int g_passed = 0;
int g_failed = 0;

// expected_outcome 取以下两种之一：
//   "OK"           —— 期望验证通过
//   "FAIL: invariant <N>"  —— 期望失败到第 N 条不变式（前缀匹配，详细 msg 不参与对比）
void RunCase(const std::string& name, const std::string& input, int expected_height,
             const std::string& expected_outcome) {
  auto pr = ParsePrintOutput(input);
  std::string actual;
  if (!pr.ok) {
    actual = "PARSE_FAIL: " + pr.msg;
  } else {
    actual = Verify(pr.tree, expected_height);
  }
  bool match = (expected_outcome == "OK")
                   ? (actual == "OK")
                   : (actual.rfind(expected_outcome, 0) == 0);
  if (match) {
    ++g_passed;
    std::cout << "[PASS] " << name << "\n";
  } else {
    ++g_failed;
    std::cout << "[FAIL] " << name << "\n  expected: " << expected_outcome << "\n  actual:   " << actual << "\n";
  }
}

}  // namespace

int main() {
  // ------------------------------------------------------------------
  // 通过用例
  // ------------------------------------------------------------------

  RunCase("empty index (root=NULL)",
    "META root=NULL\n",
    /*expected_height=*/0, "OK");

  RunCase("single-leaf root, empty table",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=0\n",
    1, "OK");

  RunCase("single-leaf root, 3 rows",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=3 | values=(1);heap_rid=(0,0);ptr=(0,0) | values=(2);heap_rid=(0,1);ptr=(0,1) | values=(3);heap_rid=(0,2);ptr=(0,2)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,1) values=(2)\n"
    "HEAP rid=(0,2) values=(3)\n",
    1, "OK");

  RunCase("2-level tree, 2 leaves",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=3 slots=2 | values=(1);heap_rid=(0,0);ptr=(0,0) | values=(2);heap_rid=(0,1);ptr=(0,1)\n"
    "INTERNAL page=2 slots=2 | values=(1);heap_rid=(0,0);ptr=(1,0) | values=(3);heap_rid=(0,2);ptr=(3,0)\n"
    "LEAF page=3 prev=1 next=NULL slots=2 | values=(3);heap_rid=(0,2);ptr=(0,2) | values=(4);heap_rid=(0,3);ptr=(0,3)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,1) values=(2)\n"
    "HEAP rid=(0,2) values=(3)\n"
    "HEAP rid=(0,3) values=(4)\n",
    2, "OK");

  RunCase("varchar column",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=2 | values=(apple);heap_rid=(0,0);ptr=(0,0) | values=(banana);heap_rid=(0,1);ptr=(0,1)\n"
    "HEAP rid=(0,0) values=(apple)\n"
    "HEAP rid=(0,1) values=(banana)\n",
    1, "OK");

  RunCase("composite (int, varchar) duplicates broken by heap_rid",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=3 | values=(1,a);heap_rid=(0,0);ptr=(0,0) | values=(1,a);heap_rid=(0,2);ptr=(0,2) | values=(2,b);heap_rid=(0,1);ptr=(0,1)\n"
    "HEAP rid=(0,0) values=(1,a)\n"
    "HEAP rid=(0,1) values=(2,b)\n"
    "HEAP rid=(0,2) values=(1,a)\n",
    1, "OK");

  // ------------------------------------------------------------------
  // 故意触发每一条不变式
  // ------------------------------------------------------------------

  // B9: SetRoot 没调
  RunCase("invariant 1: root=NULL but page exists",
    "META root=NULL\n"
    "LEAF page=1 prev=NULL next=NULL slots=0\n",
    0, "FAIL: invariant 1");

  // B6: internal slot ptr 指向不存在的页
  RunCase("invariant 1: internal points to missing page",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=NULL slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=2 | values=(1);heap_rid=(0,0);ptr=(1,0) | values=(99);heap_rid=(0,9);ptr=(99,0)\n"
    "HEAP rid=(0,0) values=(1)\n",
    2, "FAIL: invariant 1");

  // B6: internal slot.ptr.slot_id != 0
  RunCase("invariant 1: internal slot ptr.slot_id != 0",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=NULL slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=1 | values=(1);heap_rid=(0,0);ptr=(1,5)\n"
    "HEAP rid=(0,0) values=(1)\n",
    2, "FAIL: invariant 1");

  // B8: 叶子未被 internal 登记 → 孤儿
  RunCase("invariant 1: orphan leaf",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=3 slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=1 | values=(1);heap_rid=(0,0);ptr=(1,0)\n"
    "LEAF page=3 prev=1 next=NULL slots=1 | values=(3);heap_rid=(0,2);ptr=(0,2)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,2) values=(3)\n",
    2, "FAIL: invariant 1");

  // B5: leaf next=NULL，但还有别的叶子
  RunCase("invariant 2: leaf chain truncated",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=NULL slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=2 | values=(1);heap_rid=(0,0);ptr=(1,0) | values=(3);heap_rid=(0,2);ptr=(3,0)\n"
    "LEAF page=3 prev=NULL next=NULL slots=1 | values=(3);heap_rid=(0,2);ptr=(0,2)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,2) values=(3)\n",
    2, "FAIL: invariant 2");

  // B5: leaf prev 不一致
  RunCase("invariant 2: leaf prev inconsistent",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=3 slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=2 | values=(1);heap_rid=(0,0);ptr=(1,0) | values=(3);heap_rid=(0,2);ptr=(3,0)\n"
    "LEAF page=3 prev=99 next=NULL slots=1 | values=(3);heap_rid=(0,2);ptr=(0,2)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,2) values=(3)\n",
    2, "FAIL: invariant 2");

  // B2: 同页 slot 顺序错（ascending broken in-page）
  RunCase("invariant 3: within-page slot order broken",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=2 | values=(2);heap_rid=(0,0);ptr=(0,0) | values=(1);heap_rid=(0,1);ptr=(0,1)\n"
    "HEAP rid=(0,0) values=(2)\n"
    "HEAP rid=(0,1) values=(1)\n",
    1, "FAIL: invariant 3");

  // B3: 排序丢 heap_rid tiebreaker，重复 key 之间不严格单调（同 values 同 heap_rid）
  RunCase("invariant 3: extended_key tie not broken",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=2 | values=(5);heap_rid=(0,0);ptr=(0,0) | values=(5);heap_rid=(0,0);ptr=(0,0)\n"
    "HEAP rid=(0,0) values=(5)\n",
    1, "FAIL: invariant 3");

  // 跨叶子页降序
  RunCase("invariant 3: cross-leaf descending",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=3 slots=1 | values=(5);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=2 | values=(5);heap_rid=(0,0);ptr=(1,0) | values=(2);heap_rid=(0,2);ptr=(3,0)\n"
    "LEAF page=3 prev=1 next=NULL slots=1 | values=(2);heap_rid=(0,2);ptr=(0,2)\n"
    "HEAP rid=(0,0) values=(5)\n"
    "HEAP rid=(0,2) values=(2)\n",
    2, "FAIL: invariant 3");

  // B7: 父 slot key != 子 slot[0] key
  RunCase("invariant 4: parent key != child[0] (heap_rid mismatch)",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=3 slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=2 | values=(1);heap_rid=(0,0);ptr=(1,0) | values=(3);heap_rid=(0,99);ptr=(3,0)\n"
    "LEAF page=3 prev=1 next=NULL slots=1 | values=(3);heap_rid=(0,2);ptr=(0,2)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,2) values=(3)\n",
    2, "FAIL: invariant 4");

  // B4: 树高错
  RunCase("invariant 5: height mismatch",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=0\n",
    5, "FAIL: invariant 5");

  // B1/B3: 叶子 values 与堆表不匹配
  RunCase("invariant 6: leaf values disagree with heap",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=2 | values=(1);heap_rid=(0,0);ptr=(0,0) | values=(2);heap_rid=(0,1);ptr=(0,1)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,1) values=(99)\n",
    1, "FAIL: invariant 6");

  // 叶子 ptr != heap_rid（约定违背）
  RunCase("invariant 6: leaf ptr != heap_rid",
    "META root=1\n"
    "LEAF page=1 prev=NULL next=NULL slots=1 | values=(1);heap_rid=(0,0);ptr=(0,5)\n"
    "HEAP rid=(0,0) values=(1)\n",
    1, "FAIL: invariant 6");

  // B8: FinishBuild 漏登记 → 父层少一条 slot → 进入 inv4 失败（child 数量与 parent slot 数量不一致）
  // 等价表达：让 internal 只有 1 个 slot，但实际有 2 片叶子链 → 第二片叶子无 parent → inv1 抓孤儿
  RunCase("invariant 1: rightmost leaf not registered (B8)",
    "META root=2\n"
    "LEAF page=1 prev=NULL next=3 slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=1 | values=(1);heap_rid=(0,0);ptr=(1,0)\n"
    "LEAF page=3 prev=1 next=NULL slots=1 | values=(3);heap_rid=(0,2);ptr=(0,2)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,2) values=(3)\n",
    2, "FAIL: invariant 1");

  // 全树深度均衡：sibling 同类型可绕过 (e)，但跨子树深度不均。
  // root(4) → {C1=2, C2=3}（都 internal，过 (e)）；C1 → leaf=1（depth 2），
  // C2 → C3=6 → leaf=5（depth 3）。仅 (f) 能抓。
  RunCase("invariant 1: leaf depth not uniform",
    "META root=4\n"
    "LEAF page=1 prev=NULL next=5 slots=1 | values=(1);heap_rid=(0,0);ptr=(0,0)\n"
    "INTERNAL page=2 slots=1 | values=(1);heap_rid=(0,0);ptr=(1,0)\n"
    "INTERNAL page=3 slots=1 | values=(2);heap_rid=(0,1);ptr=(6,0)\n"
    "INTERNAL page=4 slots=2 | values=(1);heap_rid=(0,0);ptr=(2,0) | values=(2);heap_rid=(0,1);ptr=(3,0)\n"
    "LEAF page=5 prev=1 next=NULL slots=1 | values=(2);heap_rid=(0,1);ptr=(0,1)\n"
    "INTERNAL page=6 slots=1 | values=(2);heap_rid=(0,1);ptr=(5,0)\n"
    "HEAP rid=(0,0) values=(1)\n"
    "HEAP rid=(0,1) values=(2)\n",
    3, "FAIL: invariant 1");

  std::cout << "\n" << g_passed << " passed, " << g_failed << " failed\n";
  return g_failed == 0 ? 0 : 1;
}
