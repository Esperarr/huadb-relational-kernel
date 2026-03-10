#pragma once

#include "common/value.h"
#include "index/btree_index.h"
#include "operators/index_scan_operator.h"

namespace huadb {

class BTreeScanOperator : public IndexScanOperator {
 public:
  BTreeScanOperator(std::shared_ptr<ColumnList> column_list, oid_t table_oid, std::string table_name,
                    std::optional<std::string> alias, bool has_lock, const std::shared_ptr<BTreeIndex> index,
                    const std::vector<std::shared_ptr<Segment>> segments)
      : IndexScanOperator(std::move(column_list), table_oid, std::move(table_name), std::move(alias), has_lock,
                          OperatorType::BTREESCAN),
        index_(std::move(index)),
        segments_(std::move(segments)) {}

  std::string ToString(size_t indent_num = 0) const override {
    auto res = IndexScanOperator::ToString(indent_num);
    for (int i = 0; i < segments_.size(); ++i) {
      res += '\n';
      res += std::string(indent_num * 2 + 2, ' ') + "Segment " + std::to_string(i) + ": ";
      std::string left_bracket_ = segments_[i]->left_eq_ ? "[" : "(";
      std::string right_bracket_ = segments_[i]->right_eq_ ? "]" : ")";
      auto index_columns_ = index_->GetColumns();
      for (int j = 0; j < index_columns_->Length(); ++j) {
        res += index_columns_->GetColumn(j).GetName() + ": ";
        res += left_bracket_ + segments_[i]->left_val_[j].ToString() + ", " + segments_[i]->right_val_[j].ToString() +
               right_bracket_;
        if (j < index_columns_->Length() - 1) {
          res += ", ";
        }
      }
    }
    return res;
  }

  const std::vector<std::shared_ptr<Segment>> &GetSegments() const { return segments_; }
  const std::shared_ptr<BTreeIndex> &GetIndex() const { return index_; }

 private:
  const std::shared_ptr<BTreeIndex> index_;
  const std::vector<std::shared_ptr<Segment>> segments_;
};

}  // namespace huadb
