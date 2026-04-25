#include "table/table_scan.h"

#include "table/table_page.h"

namespace huadb {

TableScan::TableScan(BufferPool &buffer_pool, std::shared_ptr<Table> table, Rid rid)
    : buffer_pool_(buffer_pool), table_(std::move(table)), rid_(rid) {}

std::shared_ptr<Record> TableScan::GetNextRecord(xid_t xid, IsolationLevel isolation_level, cid_t cid,
                                                 const std::unordered_set<xid_t> &active_xids) {
  // 根据事务隔离级别及活跃事务集合，判断记录是否可见
  // LAB 3 BEGIN

  // 每次调用读取一条记录
  // 读取时更新 rid_ 变量，避免重复读取
  // 扫描结束时，返回空指针
  // 注意处理扫描空表的情况（rid_.page_id_ 为 NULL_PAGE_ID）
  // LAB 1 BEGIN
  if (rid_.page_id_ == NULL_PAGE_ID) {
    return nullptr;
  }

  pageid_t page_id = rid_.page_id_;
  slotid_t slot_id = rid_.slot_id_;

  while (page_id != NULL_PAGE_ID) {
    auto page = buffer_pool_.GetPage(table_->GetDbOid(), table_->GetOid(), page_id);
    TablePage table_page(page);
    db_size_t total = table_page.GetRecordCount();
    // 从当前 rid_ 开始扫描，直到找到一条未删除的记录或扫描结束
    for (; rid_.slot_id_ < total; rid_.slot_id_++) {
      auto record = table_page.GetRecord(rid_, table_->GetColumnList());
      if (IsVisible(*record, xid, isolation_level, cid, active_xids)) {
        rid_.slot_id_++;
        return record;
      }
    }
    // 移动到下一个页面
    page_id = table_page.GetNextPageId();
    slot_id = 0;
    rid_ = {page_id, slot_id};
  }
  return nullptr;
}
bool TableScan::IsVisible(const Record &record, xid_t xid, IsolationLevel isolation_level,
                          cid_t cid, const std::unordered_set<xid_t> &active_xids) const {
  xid_t xmin = record.GetXmin();
  xid_t xmax = record.GetXmax();
  cid_t rcid = record.GetCid();
  // std::cerr << "[IsVis] xmin=" << xmin << " xmax=" << xmax
  //         << " rcid=" << rcid << " xid=" << xid << " cid=" << cid << "\n";
  // 判断"插入"对当前事务是否可见
  bool xmin_visible = false;
  if (xmin == xid) {
    // 自己事务插入的
    // 万圣节问题：不能看到当前 SQL 自己插入的（cid 比当前大或相等）
    if (rcid >= cid) return false;
    xmin_visible = true;
  } else {
    // 别的事务插入的
    // 通用规则（同时适用 RC/RR/SER）：xmin 不在我的活跃集合里 ——在我"快照点"时它已结束——可见;xmin 在我的活跃集合里——在我"快照点"时还活跃——不可见
    // 本框架 active_xids 由 SeqScan 根据隔离级别选择不同时机取得：RC: 当前调用时刻的活跃集合（每条 SQL 重新拍）;RR/SER: 事务开始时的活跃集合
    if (active_xids.count(xmin) > 0) {
      xmin_visible = false;
    } else {
      // 还要排除"在我快照点之后才启动并提交"的事务（仅 RR/SER 需要）
      if (isolation_level == IsolationLevel::REPEATABLE_READ ||
          isolation_level == IsolationLevel::SERIALIZABLE) {
        if (xmin > xid) {
          xmin_visible = false;
        } else {
          xmin_visible = true;
        }
      } else {
        // RC：只要它已结束（不在活跃集合里）就可见
        xmin_visible = true;
      }
    }
  }
  if (!xmin_visible) return false;
  // 判断"删除"是否使本记录对当前事务不可见
  if (xmax == NULL_XID) {
    return true;  // 未被删除
  }
  if (xmax == xid) {
    // 自己删的（DeleteRecord 没传 cid，所以这里直接当作"已不可见"）
    return false;
  } else {
    if (active_xids.count(xmax) > 0) {
      // 删除者在我快照下还活跃—— 删除对我无效，还能看到
      return true;
    }
    // 删除者已结束
    if (isolation_level == IsolationLevel::REPEATABLE_READ ||
        isolation_level == IsolationLevel::SERIALIZABLE) {
      if (xmax > xid) {
        // 我开始之后才启动并提交的事务的删除——对我无效
        return true;
      }
    }
    // RC（或 xmax<=xid 的 RR/SER）：删除者已提交且对我可见——已删
    return false;
  }
}
}  // namespace huadb
