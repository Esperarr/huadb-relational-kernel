#include "log/log_manager.h"

#include "common/exceptions.h"
#include "log/log_records/log_records.h"

namespace huadb {

LogManager::LogManager(Disk &disk, TransactionManager &transaction_manager, lsn_t next_lsn)
    : disk_(disk), transaction_manager_(transaction_manager), next_lsn_(next_lsn), flushed_lsn_(next_lsn - 1) {}

void LogManager::SetBufferPool(std::shared_ptr<BufferPool> buffer_pool) { buffer_pool_ = std::move(buffer_pool); }

void LogManager::SetCatalog(std::shared_ptr<Catalog> catalog) { catalog_ = std::move(catalog); }

lsn_t LogManager::GetNextLSN() const { return next_lsn_; }

void LogManager::Clear() {
  std::unique_lock lock(log_buffer_mutex_);
  log_buffer_.clear();
}

void LogManager::Flush() { Flush(NULL_LSN); }

void LogManager::SetDirty(oid_t oid, pageid_t page_id, lsn_t lsn) {
  if (dpt_.find({oid, page_id}) == dpt_.end()) {
    dpt_[{oid, page_id}] = lsn;
  }
}

lsn_t LogManager::AppendInsertLog(xid_t xid, oid_t oid, pageid_t page_id, slotid_t slot_id, db_size_t offset,
                                  db_size_t size, char *new_record) {
  if (att_.find(xid) == att_.end()) {
    throw DbException(std::to_string(xid) + " does not exist in att (in AppendInsertLog)");
  }
  auto log = std::make_shared<InsertLog>(NULL_LSN, xid, att_.at(xid), oid, page_id, slot_id, offset, size, new_record);
  lsn_t lsn = next_lsn_.fetch_add(log->GetSize(), std::memory_order_relaxed);
  log->SetLSN(lsn);
  att_[xid] = lsn;
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(log));
  }
  if (dpt_.find({oid, page_id}) == dpt_.end()) {
    dpt_[{oid, page_id}] = lsn;
  }
  return lsn;
}

lsn_t LogManager::AppendDeleteLog(xid_t xid, oid_t oid, pageid_t page_id, slotid_t slot_id) {
  if (att_.find(xid) == att_.end()) {
    throw DbException(std::to_string(xid) + " does not exist in att (in AppendDeleteLog)");
  }
  auto log = std::make_shared<DeleteLog>(NULL_LSN, xid, att_.at(xid), oid, page_id, slot_id);
  lsn_t lsn = next_lsn_.fetch_add(log->GetSize(), std::memory_order_relaxed);
  log->SetLSN(lsn);
  att_[xid] = lsn;
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(log));
  }
  if (dpt_.find({oid, page_id}) == dpt_.end()) {
    dpt_[{oid, page_id}] = lsn;
  }
  return lsn;
}

lsn_t LogManager::AppendNewPageLog(xid_t xid, oid_t oid, pageid_t prev_page_id, pageid_t page_id) {
  if (xid != DDL_XID && att_.find(xid) == att_.end()) {
    throw DbException(std::to_string(xid) + " does not exist in att (in AppendNewPageLog)");
  }
  xid_t log_xid;
  if (xid == DDL_XID) {
    log_xid = NULL_XID;
  } else {
    log_xid = att_.at(xid);
  }
  auto log = std::make_shared<NewPageLog>(NULL_LSN, xid, log_xid, oid, prev_page_id, page_id);
  lsn_t lsn = next_lsn_.fetch_add(log->GetSize(), std::memory_order_relaxed);
  log->SetLSN(lsn);

  if (xid != DDL_XID) {
    att_[xid] = lsn;
  }
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(log));
  }
  if (dpt_.find({oid, page_id}) == dpt_.end()) {
    dpt_[{oid, page_id}] = lsn;
  }
  if (prev_page_id != NULL_PAGE_ID && dpt_.find({oid, prev_page_id}) == dpt_.end()) {
    dpt_[{oid, prev_page_id}] = lsn;
  }
  return lsn;
}

lsn_t LogManager::AppendBeginLog(xid_t xid) {
  if (att_.find(xid) != att_.end()) {
    throw DbException(std::to_string(xid) + " already exists in att");
  }
  auto log = std::make_shared<BeginLog>(NULL_LSN, xid, NULL_LSN);
  lsn_t lsn = next_lsn_.fetch_add(log->GetSize(), std::memory_order_relaxed);
  log->SetLSN(lsn);
  att_[xid] = lsn;
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(log));
  }
  return lsn;
}

lsn_t LogManager::AppendCommitLog(xid_t xid) {
  if (att_.find(xid) == att_.end()) {
    throw DbException(std::to_string(xid) + " does not exist in att (in AppendCommitLog)");
  }
  auto log = std::make_shared<CommitLog>(NULL_LSN, xid, att_.at(xid));
  lsn_t lsn = next_lsn_.fetch_add(log->GetSize(), std::memory_order_relaxed);
  log->SetLSN(lsn);
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(log));
  }
  Flush(lsn);
  att_.erase(xid);
  return lsn;
}

lsn_t LogManager::AppendRollbackLog(xid_t xid) {
  if (att_.find(xid) == att_.end()) {
    throw DbException(std::to_string(xid) + " does not exist in att (in AppendRollbackLog)");
  }
  auto log = std::make_shared<RollbackLog>(NULL_LSN, xid, att_.at(xid));
  lsn_t lsn = next_lsn_.fetch_add(log->GetSize(), std::memory_order_relaxed);
  log->SetLSN(lsn);
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(log));
  }
  Flush(lsn);
  att_.erase(xid);
  return lsn;
}

lsn_t LogManager::Checkpoint(bool async) {
  auto begin_checkpoint_log = std::make_shared<BeginCheckpointLog>(NULL_LSN, NULL_XID, NULL_LSN);
  lsn_t begin_lsn = next_lsn_.fetch_add(begin_checkpoint_log->GetSize(), std::memory_order_relaxed);
  begin_checkpoint_log->SetLSN(begin_lsn);
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(begin_checkpoint_log));
  }

  auto end_checkpoint_log = std::make_shared<EndCheckpointLog>(NULL_LSN, NULL_XID, NULL_LSN, att_, dpt_);
  lsn_t end_lsn = next_lsn_.fetch_add(end_checkpoint_log->GetSize(), std::memory_order_relaxed);
  end_checkpoint_log->SetLSN(end_lsn);
  {
    std::unique_lock lock(log_buffer_mutex_);
    log_buffer_.push_back(std::move(end_checkpoint_log));
  }
  Flush(end_lsn);
  std::ofstream out(MASTER_RECORD_NAME);
  out << begin_lsn;
  return end_lsn;
}

void LogManager::FlushPage(oid_t table_oid, pageid_t page_id, lsn_t page_lsn) {
  // 索引页面暂不考虑事务
  if (page_lsn == NULL_LSN) {
    return;
  }
  Flush(page_lsn);
  dpt_.erase({table_oid, page_id});
}

void LogManager::Rollback(xid_t xid) {
  // 在 att_ 中查找事务 xid 的最后一条日志的 lsn
  // 依次获取 lsn 的 prev_lsn_，直到 NULL_LSN
  // 根据 lsn 和 flushed_lsn_ 的大小关系，判断日志在 buffer 中还是在磁盘中
  // 若日志在 buffer 中，通过 log_buffer_ 获取日志
  // 若日志在磁盘中，通过 disk_ 读取日志，count 参数可设置为 MAX_LOG_SIZE
  // 通过 LogRecord::DeserializeFrom 函数解析日志
  // 调用日志的 Undo 函数
  // LAB 2 BEGIN
  if (att_.find(xid) == att_.end()) {
    return;
  }
  lsn_t lsn = att_.at(xid);
  while (lsn != NULL_LSN) {
    std::shared_ptr<LogRecord> log_record;
    if (flushed_lsn_==NULL_LSN||lsn > flushed_lsn_) {
      // 日志在 log_buffer_ 中
      std::shared_lock lock(log_buffer_mutex_);
      for (const auto &log : log_buffer_) {
        if (log->GetLSN() == lsn) {
          log_record = log;
          break;
        }
      }
    } else {
      // 日志在磁盘中，从磁盘读取
      auto data = std::make_unique<char[]>(MAX_LOG_SIZE);
      disk_.ReadLog(lsn, MAX_LOG_SIZE, data.get());
      log_record = LogRecord::DeserializeFrom(lsn, data.get());
    }
    if(log_record==nullptr){
      break;
    }
    lsn_t prev_lsn = log_record->GetPrevLSN();
    log_record->Undo(*buffer_pool_, *catalog_, *this, NULL_LSN);
    lsn = prev_lsn;
  }
}

void LogManager::Recover() {
  Analyze();
  Redo();
  Undo();
}

void LogManager::IncrementRedoCount() { redo_count_++; }

uint32_t LogManager::GetRedoCount() const { return redo_count_; }

void LogManager::Flush(lsn_t lsn) {
  size_t max_log_size = 0;
  lsn_t max_lsn = NULL_LSN;
  {
    std::unique_lock lock(log_buffer_mutex_);
    for (auto iterator = log_buffer_.cbegin(); iterator != log_buffer_.cend();) {
      const auto &log_record = *iterator;
      // 如果 lsn 为 NULL_LSN，表示 log_buffer_ 中所有日志都需要刷盘
      if (lsn != NULL_LSN && log_record->GetLSN() > lsn) {
        iterator++;
        continue;
      }
      auto log_size = log_record->GetSize();
      auto log = std::make_unique<char[]>(log_size);
      log_record->SerializeTo(log.get());
      disk_.WriteLog(log_record->GetLSN(), log_size, log.get());
      if (max_lsn == NULL_LSN || log_record->GetLSN() > max_lsn) {
        max_lsn = log_record->GetLSN();
        max_log_size = log_size;
      }
      iterator = log_buffer_.erase(iterator);
    }
  }
  // 如果 max_lsn 为 NULL_LSN，表示没有日志刷盘
  // 如果 flushed_lsn_ 为 NULL_LSN，表示还没有日志刷过盘
  if (max_lsn != NULL_LSN && (flushed_lsn_ == NULL_LSN || max_lsn > flushed_lsn_)) {
    flushed_lsn_ = max_lsn;
    lsn_t next_lsn = FIRST_LSN;
    if (disk_.FileExists(NEXT_LSN_NAME)) {
      std::ifstream in(NEXT_LSN_NAME);
      in >> next_lsn;
    }
    if (flushed_lsn_ + max_log_size > next_lsn) {
      std::ofstream out(NEXT_LSN_NAME);
      out << (flushed_lsn_ + max_log_size);
    }
  }
}

void LogManager::Analyze() {
  // 恢复 Master Record 等元信息
  // 恢复故障时正在使用的数据库
  if (disk_.FileExists(NEXT_LSN_NAME)) {
    std::ifstream in(NEXT_LSN_NAME);
    lsn_t next_lsn;
    in >> next_lsn;
    next_lsn_ = next_lsn;
  } else {
    next_lsn_ = FIRST_LSN;
  }
  flushed_lsn_ = next_lsn_ - 1;
  lsn_t checkpoint_lsn = 0;

  if (disk_.FileExists(MASTER_RECORD_NAME)) {
    std::ifstream in(MASTER_RECORD_NAME);
    in >> checkpoint_lsn;
  }
  // 根据 Checkpoint 日志恢复脏页表、活跃事务表等元信息
  // 必要时调用 transaction_manager_.SetNextXid 来恢复事务 id
  // LAB 2 BEGIN
  // 从checkpoint_lsn开始读取日志，恢复att_和dpt_
  lsn_t lsn = checkpoint_lsn;
  xid_t max_xid = 0;
  while (lsn < next_lsn_) {
    auto data = std::make_unique<char[]>(MAX_LOG_SIZE);
    disk_.ReadLog(lsn, MAX_LOG_SIZE, data.get());
    auto log_record = LogRecord::DeserializeFrom(lsn, data.get());
    if(log_record->GetSize() == 0) {
      break;
    }
    LogType log_type = log_record->GetType();
    xid_t xid = log_record->GetXid();
    // 跟踪日志中出现过的最大 xid
    if (xid != NULL_XID && xid != DDL_XID && xid > max_xid) {
      max_xid = xid;
    }
    if (log_type == LogType::END_CHECKPOINT) {
      auto end_checkpoint_log = std::dynamic_pointer_cast<EndCheckpointLog>(log_record);
      if (end_checkpoint_log) {
        att_ = end_checkpoint_log->GetATT();
        dpt_ = end_checkpoint_log->GetDPT();
      }
    } else if (log_type == LogType::BEGIN) {
      if (att_.find(xid) == att_.end()) {
        att_[xid] = lsn;
      }
    } else if (log_type == LogType::COMMIT || log_type == LogType::ROLLBACK) {
      att_.erase(xid);
    } else if (log_type == LogType::INSERT || log_type == LogType::DELETE || log_type == LogType::NEW_PAGE) {
      if (xid != NULL_XID) {
        att_[xid] = lsn;
      }
      if (log_type == LogType::INSERT) {
        auto insert_log = std::dynamic_pointer_cast<InsertLog>(log_record);
        if (insert_log) {
          SetDirty(insert_log->GetOid(), insert_log->GetPageId(), lsn);
        }
      } else if (log_type == LogType::DELETE) {
        auto delete_log = std::dynamic_pointer_cast<DeleteLog>(log_record);
        if (delete_log) {
          SetDirty(delete_log->GetOid(), delete_log->GetPageId(), lsn);
        }
      } else if (log_type == LogType::NEW_PAGE) {
        auto new_page_log = std::dynamic_pointer_cast<NewPageLog>(log_record);
        if (new_page_log) {
          SetDirty(new_page_log->GetOid(), new_page_log->GetPageId(), lsn);
          if (new_page_log->GetPrevPageId() != NULL_PAGE_ID) {
            SetDirty(new_page_log->GetOid(), new_page_log->GetPrevPageId(), lsn);
          }
        }
      }
    }
    lsn += log_record->GetSize();
  }
  // 循环结束后，把 next_xid 推到大于日志中所有 xid
  if (max_xid > 0) {
    transaction_manager_.SetNextXid(max_xid + 1);
  }
}

void LogManager::Redo() {
  // 正序读取日志，调用日志记录的 Redo 函数
  // LAB 2 BEGIN
  lsn_t redo_start_lsn = FIRST_LSN;
  for (const auto &entry : dpt_) {
    if (entry.second < redo_start_lsn || redo_start_lsn == FIRST_LSN) {
      redo_start_lsn = entry.second;
    }
  }
  if (dpt_.empty()) {
    redo_start_lsn = FIRST_LSN;
  }
  lsn_t lsn = redo_start_lsn;
  while (lsn < next_lsn_) {
    auto data = std::make_unique<char[]>(MAX_LOG_SIZE);
    disk_.ReadLog(lsn, MAX_LOG_SIZE, data.get());
    auto log_record = LogRecord::DeserializeFrom(lsn, data.get());
    if(log_record->GetSize() == 0) {
      break;
    }
    log_record->Redo(*buffer_pool_, *catalog_, *this);
    IncrementRedoCount();
    lsn += log_record->GetSize();
  }
}

void LogManager::Undo() {
  // 根据活跃事务表，将所有活跃事务回滚
  // LAB 2 BEGIN
  // Copy the keys since Rollback might modify att_
  std::vector<xid_t> xids;
  for (const auto &entry : att_) {
    xids.push_back(entry.first);
  }
  for (xid_t xid : xids) {
    Rollback(xid);
  }
  // After undo, clear all active transactions
  att_.clear();
}

}  // namespace huadb
