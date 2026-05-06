#pragma once

#include "common/value.h"
#include "index/btree_index.h"
#include "operators/index_scan_operator.h"

namespace huadb {

class BTreeScanOperator : public IndexScanOperator {
 public:
  BTreeScanOperator(std::shared_ptr<ColumnList> column_list, oid_t table_oid, std::string table_name,
                    std::optional<std::string> alias, bool has_lock, const std::shared_ptr<BTreeIndex> index,
                    const std::shared_ptr<Segment> segment)
      : IndexScanOperator(std::move(column_list), table_oid, std::move(table_name), std::move(alias), has_lock,
                          OperatorType::BTREESCAN),
        index_(std::move(index)),
        segment_(segment) {}

  std::string ToString(size_t indent_num = 0) const override {
    auto res = IndexScanOperator::ToString(indent_num);
    res += "\n" + std::string(indent_num * 2 + 2, ' ') + "Segment: ";
    std::string left_bracket_ = segment_->left_eq_ ? "[" : "(";
    std::string right_bracket_ = segment_->right_eq_ ? "]" : ")";
    auto index_columns_ = index_->GetColumns();
    auto left_size = static_cast<int>(segment_->left_val_.size());
    auto right_size = static_cast<int>(segment_->right_val_.size());
    for (int i = 0; i < index_columns_->Length(); ++i) {
      res += index_columns_->GetColumn(i).GetName() + ": ";
      bool left_in_prefix = i < left_size;
      bool right_in_prefix = i < right_size;
      std::string lb = left_in_prefix ? left_bracket_ : "(";
      std::string rb = right_in_prefix ? right_bracket_ : ")";
      std::string lv = left_in_prefix ? segment_->left_val_[i].ToSqlLiteral() : "-inf";
      std::string rv = right_in_prefix ? segment_->right_val_[i].ToSqlLiteral() : "+inf";
      res += lb + lv + ", " + rv + rb;
      if (i < index_columns_->Length() - 1) {
        res += ", ";
      }
    }
    return res;
  }

  const std::shared_ptr<Segment> GetSegment() const { return segment_; }
  const std::shared_ptr<BTreeIndex> &GetIndex() const { return index_; }

 private:
  const std::shared_ptr<BTreeIndex> index_;
  const std::shared_ptr<Segment> segment_;
};

}  // namespace huadb
