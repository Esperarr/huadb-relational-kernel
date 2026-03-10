#include "index.h"

#include "table/table.h"

namespace huadb {

std::vector<size_t> Index::GetColumnIndices() const {
  std::vector<size_t> res;
  for (auto &col : schema_->GetColumns()) {
    res.emplace_back(table_->GetColumnList().GetColumnIndex(col.GetName()));
  }
  return res;
}

oid_t Index::GetTableOid() const { return table_->GetOid(); }

oid_t Index::GetDbOid() const { return db_oid_; }

oid_t Index::GetOid() const { return oid_; }

}  // namespace huadb
