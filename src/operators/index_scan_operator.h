#pragma once

#include "operators/operator.h"

namespace huadb {

class IndexScanOperator : public Operator {
 public:
  IndexScanOperator(std::shared_ptr<ColumnList> column_list, oid_t table_oid, std::string table_name,
                    std::optional<std::string> alias, bool has_lock, OperatorType type)
      : Operator(type, std::move(column_list), {}),
        table_oid_(table_oid),
        table_name_(std::move(table_name)),
        alias_(std::move(alias)),
        has_lock_(has_lock) {}

  std::string ToString(size_t indent_num = 0) const override {
    std::string res = "";
    std::string index_name = (type_ == OperatorType::BTREESCAN) ? "BTreeScan" : "HashScan";
    if (alias_) {
      res += fmt::format("{}{}: {} {}", std::string(indent_num * 2, ' '), index_name, table_name_, *alias_);
    } else {
      res += fmt::format("{}{}: {}", std::string(indent_num * 2, ' '), index_name, table_name_);
    }
    return res;
  }

  oid_t GetTableOid() const { return table_oid_; }
  const std::string &GetTableNameOrAlias() const {
    if (alias_) {
      return *alias_;
    } else {
      return table_name_;
    }
  }
  const std::string &GetTableName() const { return table_name_; }
  const std::optional<std::string> &GetAlias() const { return alias_; }
  bool HasLock() const { return has_lock_; }

 protected:
  oid_t table_oid_;
  std::string table_name_;
  std::optional<std::string> alias_;
  bool has_lock_;
};

}  // namespace huadb