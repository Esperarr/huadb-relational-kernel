#include "catalog/column_list.h"

namespace huadb {

// clang-format off
ColumnList table_meta_schema({ColumnDefinition("table_oid", Type::UINT),
                              ColumnDefinition("db_oid", Type::UINT),
                              ColumnDefinition("table_name", Type::VARCHAR, 32),
                              ColumnDefinition("schema", Type::VARCHAR, 1024),
                              ColumnDefinition("cardinality", Type::UINT)});
ColumnList database_meta_schema({ColumnDefinition("db_oid", Type::UINT),
                                 ColumnDefinition("db_name", Type::VARCHAR, 32)});
ColumnList statistic_schema({ColumnDefinition("table_name", Type::VARCHAR, 32),
                             ColumnDefinition("db_oid", Type::UINT),
                             ColumnDefinition("column_name", Type::VARCHAR, 32),
                             ColumnDefinition("n_distinct", Type::UINT)});
ColumnList index_meta_schema({ColumnDefinition("index_oid", Type::UINT),
                              ColumnDefinition("table_oid", Type::UINT),
                              ColumnDefinition("index_name", Type::VARCHAR, 32),
															ColumnDefinition("index_type", Type::INT),
															ColumnDefinition("column_indices", Type::VARCHAR, 32)});
// clang-format on

}  // namespace huadb
