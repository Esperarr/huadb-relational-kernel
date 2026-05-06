#pragma once

#include <string>
#include <vector>

#include "catalog/column_list.h"
#include "common/bitmap.h"
#include "common/value.h"
#include "table/record_header.h"

namespace huadb {

class Record {
 public:
  Record() = default;
  explicit Record(std::vector<Value> values, Rid rid = {0, 0});
  // 记录合并，用于 join 算子
  void Append(const Record &record);
  // 获取第 col_idx 个 column 的值
  Value GetValue(size_t col_idx) const;
  // 设置第 col_idx 个 column 的值
  void SetValue(size_t col_idx, const Value &value);
  // 获取所有 column 的值
  const std::vector<Value> &GetValues() const;
  // 获取记录的大小
  db_size_t GetSize() const;
  // 将记录转换为字符串
  std::string ToString() const;
  // 记录序列化
  db_size_t SerializeTo(char *data) const;
  // 记录反序列化
  db_size_t DeserializeFrom(const char *data, const ColumnList &column_list);
  // 记录头序列化
  void SerializeHeaderTo(char *data) const;
  // 记录头反序列化
  void DeserializeHeaderFrom(const char *data);
  // 获取记录头信息
  bool IsDeleted() const;
  xid_t GetXmin() const;
  xid_t GetXmax() const;
  cid_t GetCid() const;

  // 设置记录头信息
  void SetDeleted(bool deleted);
  void SetXmin(xid_t xmin);
  void SetXmax(xid_t xmax);
  void SetCid(cid_t cid);

  // 获取 rid
  Rid GetRid() const;
  // 设置 rid
  void SetRid(Rid rid);

 private:
  void UpdateSize();

  // 空值位图
  Bitmap null_bitmap_;
  std::vector<Value> values_;
  RecordHeader header_;
  Rid rid_;
  db_size_t size_;
};

/*
 * 索引元组，由键值、指针、heap_rid 三部分组成
 *   - values_: 索引项存储的 key（用户列）
 *   - ptr_:    指向下一层位置——内部节点指向下层页面，叶子节点指向堆表元组
 *   - heap_rid_: 比较键的隐式末位列；叶子节点为该叶子记录指向的堆表行 rid，
 *                内部节点为其子树最左叶子记录的 heap_rid（在构建时由下层向上传播）
 *                作用：让 B+ 树以 (values_, heap_rid_) 为严格有序的扩展键，
 *                      消除重复键导致的内部节点路由歧义
 */
class IndexRecord : public Record {
 public:
  IndexRecord() = default;
  IndexRecord(std::vector<Value> values, Rid ptr = {0, 0}, Rid heap_rid = {0, 0})
      : Record(values), ptr_(ptr), heap_rid_(heap_rid) {}
  db_size_t GetSize() const;
  db_size_t SerializeTo(char *data) const;
  db_size_t DeserializeFrom(const char *data, const ColumnList &column_list);
  Rid GetPtr() const;
  void SetPtr(Rid ptr);
  Rid GetHeapRid() const;
  void SetHeapRid(Rid heap_rid);
  std::string ToString();

 private:
  Rid ptr_;
  Rid heap_rid_;
};

}  // namespace huadb
