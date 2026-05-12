#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

enum class RecordType { STATEMENT, QUERY, VERIFY_BTREE };
enum class SortMode { NO_SORT, ROW_SORT, VALUE_SORT, KEYWORDS, COUNT };
enum class ResultType { SUCCESS, ERROR };

class Location {
 public:
  std::string file_;
  size_t line_;
  friend std::ostream &operator<<(std::ostream &os, const Location &loc) {
    os << loc.file_ << ":" << loc.line_;
    return os;
  }
  std::string ToString() { return file_ + ":" + std::to_string(line_); }
};

class Record {
 public:
  Record(RecordType type, Location loc) : type_(type), loc_(std::move(loc)) {}
  virtual ~Record() = default;
  RecordType type_;
  Location loc_;
};

class StatementRecord : public Record {
 public:
  StatementRecord(Location loc, std::string sql, ResultType expected_result, std::string connection_name,
                  int timeout_seconds = 0)
      : Record(RecordType::STATEMENT, std::move(loc)),
        sql_(std::move(sql)),
        expected_result_(expected_result),
        connection_name_(std::move(connection_name)),
        timeout_seconds_(timeout_seconds) {}

  std::string sql_;
  ResultType expected_result_;
  std::string connection_name_;
  int timeout_seconds_;  // 0 = no timeout
};

class QueryRecord : public Record {
 public:
  QueryRecord(Location loc, std::string sql, SortMode sort_mode, std::string connection_name,
              std::string expected_result, int timeout_seconds = 0)
      : Record(RecordType::QUERY, std::move(loc)),
        sql_(std::move(sql)),
        sort_mode_(sort_mode),
        connection_name_(std::move(connection_name)),
        expected_result_(std::move(expected_result)),
        timeout_seconds_(timeout_seconds) {}

  std::string sql_;
  SortMode sort_mode_;
  std::string connection_name_;
  std::string expected_result_;
  int timeout_seconds_;  // 0 = no timeout
};

// 单行指令，语法：`verify btree <idx_name> height=<N>`
// 评测时由 sqllogictest 主循环触发：先用 connection 发 `SHOW <idx_name>` 拿 Print 字符串，
// 再喂给 test/src/btree/ 中的 verifier，断言输出等于 "OK"。
class VerifyBtreeRecord : public Record {
 public:
  VerifyBtreeRecord(Location loc, std::string idx_name, int expected_height)
      : Record(RecordType::VERIFY_BTREE, std::move(loc)),
        idx_name_(std::move(idx_name)),
        expected_height_(expected_height) {}

  std::string idx_name_;
  int expected_height_;
};

class SQLLogicParser {
 public:
  bool OpenFile(fs::path path);
  void Parse();

  std::vector<std::unique_ptr<Record>> records;

 private:
  std::vector<std::string> Tokenize();

  fs::path path_;
  std::vector<std::string> lines_;
  std::vector<std::string>::const_iterator line_iter_;
};
