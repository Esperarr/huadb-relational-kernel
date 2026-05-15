// B+ 树不变式验证器。
//
// 设计目标：可扩展。增加一条新的 invariant 只需要在 verifier.cpp 的
// kInvariants 列表里追加一项 {id, name, check_fn}，无需改动任何调用方。
//
// 不变式按 id 顺序逐条跑，第一条失败处停下并返回 "FAIL: invariant <id>: <msg>"。
// 全部通过则返回 "OK"。

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "parser.h"

namespace huadb::btree_test {

struct CheckResult {
  bool ok;
  std::string msg;  // 失败诊断；ok=true 时为空
};

struct Invariant {
  int id;            // 失败消息中显示的编号
  std::string name;  // 仅供日志/调试可读
  std::function<CheckResult(const PrintedTree&, int /*expected_height*/)> check;
};

// 当前注册的所有不变式（按 id 排序）。新增 invariant 在 verifier.cpp 中追加。
const std::vector<Invariant>& GetInvariants();

// 跑全部不变式。成功："OK"；失败："FAIL: invariant <id>: <msg>"。
std::string Verify(const PrintedTree& tree, int expected_height);

}  // namespace huadb::btree_test
