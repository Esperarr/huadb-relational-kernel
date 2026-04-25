#include "transaction/lock_manager.h"

namespace huadb {

bool LockManager::LockTable(xid_t xid, LockType lock_type, oid_t oid) {
  // 对数据表加锁，成功加锁返回 true，如果数据表已被其他事务加锁，且锁的类型不相容，返回 false
  // 如果本事务已经持有该数据表的锁，根据需要升级锁的类型
  // LAB 3 BEGIN
  std::lock_guard<std::mutex> lk(latch_);
  auto &holders = table_locks_[oid];
  // 看本事务是否已持有该表的锁，如果有则升级
  for (auto &h : holders) {
    if (h.xid_ == xid) {
      LockType new_type = Upgrade(h.type_, lock_type);
      if (new_type == h.type_) return true;  // 不需要升级
      // 检查升级后是否与其它事务相容
      for (auto &other : holders) {
        if (other.xid_ != xid && !Compatible(new_type, other.type_)) {
          return false;
        }
      }
      h.type_ = new_type;
      return true;
    }
  }

  // 全新加锁：检查与其他事务的锁是否兼容
  for (auto &other : holders) {
    if (!Compatible(lock_type, other.type_)) {
      return false;
    }
  }
  holders.push_back({xid, lock_type});
  return true;
}

bool LockManager::LockRow(xid_t xid, LockType lock_type, oid_t oid, Rid rid) {
  // 对数据行加锁，成功加锁返回 true，如果数据行已被其他事务加锁，且锁的类型不相容，返回 false
  // 如果本事务已经持有该数据行的锁，根据需要升级锁的类型
  // LAB 3 BEGIN
  std::lock_guard<std::mutex> lk(latch_);
  auto key = std::make_pair(oid, rid);
  auto &holders = row_locks_[key];

  for (auto &h : holders) {
    if (h.xid_ == xid) {
      LockType new_type = Upgrade(h.type_, lock_type);
      if (new_type == h.type_) return true;
      for (auto &other : holders) {
        if (other.xid_ != xid && !Compatible(new_type, other.type_)) {
          return false;
        }
      }
      h.type_ = new_type;
      return true;
    }
  }

  for (auto &other : holders) {
    if (!Compatible(lock_type, other.type_)) {
      return false;
    }
  }
  holders.push_back({xid, lock_type});
  return true;
}

void LockManager::ReleaseLocks(xid_t xid) {
  // 释放事务 xid 持有的所有锁
  // LAB 3 BEGIN
  std::lock_guard<std::mutex> lk(latch_);
  for (auto &kv : table_locks_) {
    auto &v = kv.second;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [xid](const LockHolder &h) { return h.xid_ == xid; }),
            v.end());
  }
  for (auto &kv : row_locks_) {
    auto &v = kv.second;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [xid](const LockHolder &h) { return h.xid_ == xid; }),
            v.end());
  }
}

void LockManager::SetDeadLockType(DeadlockType deadlock_type) { deadlock_type_ = deadlock_type; }

bool LockManager::Compatible(LockType type_a, LockType type_b) const {
  // 判断锁是否相容
  // LAB 3 BEGIN
  if (type_a == LockType::X || type_b == LockType::X) return false;
  if (type_a == LockType::IS) return true;  // IS 与 IS/IX/S/SIX 都兼容
  if (type_b == LockType::IS) return true;
  if (type_a == LockType::SIX || type_b == LockType::SIX) return false;
  // 此时 a, b ∈ {IX, S}
  if (type_a == LockType::IX && type_b == LockType::IX) return true;
  if (type_a == LockType::S && type_b == LockType::S) return true;
  // 剩下的就是 IX 和 S 的组合
  return false;
}

LockType LockManager::Upgrade(LockType self, LockType other) const {
  // 升级锁类型
  // LAB 3 BEGIN
  // 锁强度：IS < IX,S < SIX < X
  // X 最强
  if (self == LockType::X || other == LockType::X) return LockType::X;
  // S + IX = SIX，IX + S = SIX
  if ((self == LockType::S && other == LockType::IX) ||
      (self == LockType::IX && other == LockType::S)) {
    return LockType::SIX;
  }
  // SIX 大于 IS/IX/S
  if (self == LockType::SIX || other == LockType::SIX) return LockType::SIX;
  // 任一是 IS 就取另一个（IS 最弱）
  if (self == LockType::IS) return other;
  if (other == LockType::IS) return self;
  // 同类
  return self;
}

}  // namespace huadb
