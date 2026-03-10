#pragma once

#include <optional>
#include <set>

#include "fmt/format.h"
#include "operators/operator.h"

namespace huadb {

class RidUnionOperator : public Operator {
 public:
  RidUnionOperator(std::shared_ptr<ColumnList> column_list, std::vector<std::shared_ptr<Operator>> children)
      : Operator(OperatorType::RID_UNION, std::move(column_list), std::move(children)) {}
  std::string ToString(size_t indent_num = 0) const override {
    auto s = fmt::format("{}RidUnion: ", std::string(indent_num * 2, ' '));
    for (size_t i = 0; i < children_.size(); i++) {
      s += "\n" + std::string(indent_num * 2 + 2, ' ') + children_[i]->ToString(indent_num + 2);
    }
    return s;
  }
};

}  // namespace huadb
