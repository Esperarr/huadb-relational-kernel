#include "storage/lru_buffer_strategy.h"

namespace huadb {

void LRUBufferStrategy::Access(size_t frame_no) {
  // 缓存页面访问
  // LAB 1 BEGIN
  lru_list_.remove(frame_no);      // 查找+删除
  lru_list_.push_front(frame_no);
};

size_t LRUBufferStrategy::Evict() {
  // 缓存页面淘汰，返回淘汰的页面在 buffer pool 中的下标
  // LAB 1 BEGIN
  size_t victim = lru_list_.back();
  lru_list_.pop_back();
  return victim;
}

}  // namespace huadb
