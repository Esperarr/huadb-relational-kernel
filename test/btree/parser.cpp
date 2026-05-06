// B+ 树 Print 输出解析器实现。
//
// 输入约定（每行第一个 token 即类型标签）：
//   META root=<id|NULL>
//   LEAF page=<id> prev=<id|NULL> next=<id|NULL> slots=<n> | <slot_0> | ... | <slot_{n-1}>
//   INTERNAL page=<id> slots=<n> | <slot_0> | ... | <slot_{n-1}>
//   HEAP rid=(<p>,<s>) values=(<v1>,<v2>,...)
// 每个 slot：values=(<v1>,<v2>,...);heap_rid=(<p>,<s>);ptr=(<p>,<s>)
//
// 注意：当 SHOW 经过 SimpleWriter 输出时，每行末尾会带一个 cell-separator
// 空格。解析器在 Trim 阶段会吃掉，无需特别处理。

#include "parser.h"

#include <stdexcept>

namespace huadb::btree_test {

namespace {

std::vector<std::string> Split(const std::string &s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      out.emplace_back(std::move(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.emplace_back(std::move(cur));
  return out;
}

// 按子串分隔（用于 " | " 这种多字符分隔符）
std::vector<std::string> SplitByStr(const std::string &s, const std::string &sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t p = s.find(sep, start);
    if (p == std::string::npos) {
      out.emplace_back(s.substr(start));
      return out;
    }
    out.emplace_back(s.substr(start, p - start));
    start = p + sep.size();
  }
}

std::string Trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}

bool StartsWith(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ParseIntOrNull(const std::string &s, int &out) {
  if (s == "NULL") {
    out = kNullPage;
    return true;
  }
  try {
    size_t pos = 0;
    out = std::stoi(s, &pos);
    return pos == s.size();
  } catch (...) {
    return false;
  }
}

// 解析 "(p,s)"
bool ParseRid(const std::string &s, PageRid &out) {
  if (s.size() < 5 || s.front() != '(' || s.back() != ')') return false;
  std::string inner = s.substr(1, s.size() - 2);
  size_t comma = inner.find(',');
  if (comma == std::string::npos) return false;
  try {
    size_t p1 = 0, p2 = 0;
    out.first = std::stoi(inner.substr(0, comma), &p1);
    out.second = std::stoi(inner.substr(comma + 1), &p2);
    return p1 == comma && p2 == inner.size() - comma - 1;
  } catch (...) {
    return false;
  }
}

// 解析 "(v1,v2,...)" 或 "()"
bool ParseValuesList(const std::string &s, std::vector<std::string> &out) {
  if (s.size() < 2 || s.front() != '(' || s.back() != ')') return false;
  std::string inner = s.substr(1, s.size() - 2);
  out = inner.empty() ? std::vector<std::string>{} : Split(inner, ',');
  return true;
}

// 解析 "values=(...);heap_rid=(p,s);ptr=(p,s)"
bool ParseSlot(const std::string &s, Slot &out, std::string &err) {
  auto parts = SplitByStr(s, ";");
  if (parts.size() != 3) {
    err = "slot expected 3 ';'-separated fields, got " + std::to_string(parts.size()) + ": '" + s + "'";
    return false;
  }
  if (!StartsWith(parts[0], "values=") || !ParseValuesList(parts[0].substr(7), out.values)) {
    err = "bad slot.values: '" + parts[0] + "'";
    return false;
  }
  if (!StartsWith(parts[1], "heap_rid=") || !ParseRid(parts[1].substr(9), out.heap_rid)) {
    err = "bad slot.heap_rid: '" + parts[1] + "'";
    return false;
  }
  if (!StartsWith(parts[2], "ptr=") || !ParseRid(parts[2].substr(4), out.ptr)) {
    err = "bad slot.ptr: '" + parts[2] + "'";
    return false;
  }
  return true;
}

bool ParseKv(const std::string &tok, const std::string &key, std::string &out) {
  if (!StartsWith(tok, key + "=")) return false;
  out = tok.substr(key.size() + 1);
  return true;
}

}  // namespace

ParseResult ParsePrintOutput(const std::string &input) {
  ParseResult R;

  for (const auto &raw_line : Split(input, '\n')) {
    auto line = Trim(raw_line);
    if (line.empty()) continue;

    auto chunks = SplitByStr(line, " | ");
    auto header = Trim(chunks[0]);

    auto first_space = header.find(' ');
    if (first_space == std::string::npos) {
      R.ok = false;
      R.msg = "no type tag in line: '" + header + "'";
      return R;
    }
    std::string tag = header.substr(0, first_space);
    std::string rest = header.substr(first_space + 1);
    auto tokens = Split(rest, ' ');

    if (tag == "META") {
      if (tokens.size() != 1) {
        R.ok = false;
        R.msg = "META expects 1 token: '" + line + "'";
        return R;
      }
      std::string v;
      if (!ParseKv(tokens[0], "root", v) || !ParseIntOrNull(v, R.tree.root_id)) {
        R.ok = false;
        R.msg = "META root parse failed: '" + tokens[0] + "'";
        return R;
      }
    } else if (tag == "LEAF") {
      if (tokens.size() != 4) {
        R.ok = false;
        R.msg = "LEAF expects 4 header tokens: '" + line + "'";
        return R;
      }
      LeafPage p;
      std::string v;
      int slot_count;
      if (!ParseKv(tokens[0], "page", v) || !ParseIntOrNull(v, p.page_id) || p.page_id == kNullPage) {
        R.ok = false;
        R.msg = "bad LEAF page: '" + tokens[0] + "'";
        return R;
      }
      if (!ParseKv(tokens[1], "prev", v) || !ParseIntOrNull(v, p.prev_id)) {
        R.ok = false;
        R.msg = "bad LEAF prev: '" + tokens[1] + "'";
        return R;
      }
      if (!ParseKv(tokens[2], "next", v) || !ParseIntOrNull(v, p.next_id)) {
        R.ok = false;
        R.msg = "bad LEAF next: '" + tokens[2] + "'";
        return R;
      }
      if (!ParseKv(tokens[3], "slots", v) || !ParseIntOrNull(v, slot_count) || slot_count < 0) {
        R.ok = false;
        R.msg = "bad LEAF slots: '" + tokens[3] + "'";
        return R;
      }
      if (chunks.size() != static_cast<size_t>(slot_count) + 1) {
        R.ok = false;
        R.msg = "LEAF page=" + std::to_string(p.page_id) + " declares slots=" + std::to_string(slot_count) +
                " but found " + std::to_string(chunks.size() - 1) + " '|'-separated slot fields";
        return R;
      }
      for (int i = 0; i < slot_count; ++i) {
        Slot s;
        std::string err;
        if (!ParseSlot(Trim(chunks[1 + i]), s, err)) {
          R.ok = false;
          R.msg = "LEAF page=" + std::to_string(p.page_id) + " slot " + std::to_string(i) + ": " + err;
          return R;
        }
        p.slots.emplace_back(std::move(s));
      }
      if (R.tree.leaves.count(p.page_id) || R.tree.internals.count(p.page_id)) {
        R.ok = false;
        R.msg = "duplicate page id " + std::to_string(p.page_id);
        return R;
      }
      R.tree.leaves[p.page_id] = std::move(p);
    } else if (tag == "INTERNAL") {
      if (tokens.size() != 2) {
        R.ok = false;
        R.msg = "INTERNAL expects 2 header tokens: '" + line + "'";
        return R;
      }
      InternalPage p;
      std::string v;
      int slot_count;
      if (!ParseKv(tokens[0], "page", v) || !ParseIntOrNull(v, p.page_id) || p.page_id == kNullPage) {
        R.ok = false;
        R.msg = "bad INTERNAL page: '" + tokens[0] + "'";
        return R;
      }
      if (!ParseKv(tokens[1], "slots", v) || !ParseIntOrNull(v, slot_count) || slot_count < 0) {
        R.ok = false;
        R.msg = "bad INTERNAL slots: '" + tokens[1] + "'";
        return R;
      }
      if (chunks.size() != static_cast<size_t>(slot_count) + 1) {
        R.ok = false;
        R.msg = "INTERNAL page=" + std::to_string(p.page_id) + " declares slots=" + std::to_string(slot_count) +
                " but found " + std::to_string(chunks.size() - 1) + " '|'-separated slot fields";
        return R;
      }
      for (int i = 0; i < slot_count; ++i) {
        Slot s;
        std::string err;
        if (!ParseSlot(Trim(chunks[1 + i]), s, err)) {
          R.ok = false;
          R.msg = "INTERNAL page=" + std::to_string(p.page_id) + " slot " + std::to_string(i) + ": " + err;
          return R;
        }
        p.slots.emplace_back(std::move(s));
      }
      if (R.tree.leaves.count(p.page_id) || R.tree.internals.count(p.page_id)) {
        R.ok = false;
        R.msg = "duplicate page id " + std::to_string(p.page_id);
        return R;
      }
      R.tree.internals[p.page_id] = std::move(p);
    } else if (tag == "HEAP") {
      if (tokens.size() != 2) {
        R.ok = false;
        R.msg = "HEAP expects 2 tokens: '" + line + "'";
        return R;
      }
      HeapRow h;
      std::string v;
      if (!ParseKv(tokens[0], "rid", v) || !ParseRid(v, h.rid)) {
        R.ok = false;
        R.msg = "bad HEAP rid: '" + tokens[0] + "'";
        return R;
      }
      if (!ParseKv(tokens[1], "values", v) || !ParseValuesList(v, h.values)) {
        R.ok = false;
        R.msg = "bad HEAP values: '" + tokens[1] + "'";
        return R;
      }
      R.tree.heap.emplace_back(std::move(h));
    } else {
      R.ok = false;
      R.msg = "unknown tag '" + tag + "' in line: '" + line + "'";
      return R;
    }
  }
  return R;
}

}  // namespace huadb::btree_test
