#include "executors/orderby_executor.h"

namespace huadb {

OrderByExecutor::OrderByExecutor(ExecutorContext &context, std::shared_ptr<const OrderByOperator> plan,
                                 std::shared_ptr<Executor> child)
    : Executor(context, {std::move(child)}), plan_(std::move(plan)) {}

void OrderByExecutor::Init() { children_[0]->Init(); }

std::shared_ptr<Record> OrderByExecutor::Next() {
  // 可以使用 STL 的 sort 函数
  // 通过 OperatorExpression 的 Evaluate 函数获取 Value 的值
  // 通过 Value 的 Less, Equal, Greater 函数比较 Value 的值
  // LAB 4 BEGIN
  if (!sorted_) {
    while (auto record = children_[0]->Next()) {
      records_.push_back(record);// 一次性拉取子算子所有记录到内存
    }
    std::sort(records_.begin(), records_.end(),
              [this](const std::shared_ptr<Record> &a, const std::shared_ptr<Record> &b) {
                // 多列排序：按 order_bys_ 的顺序依次比较，前一列相等才看下一列
                for (const auto &[type, expr] : plan_->order_bys_) {
                  Value va = expr->Evaluate(a);
                  Value vb = expr->Evaluate(b);
                  if (va.Less(vb)) {
                    // ASC / DEFAULT 视为升序，DESC 视为降序
                    return type != OrderByType::DESC;
                  }
                  if (va.Greater(vb)) {
                    return type == OrderByType::DESC;
                  }
                  // 相等则继续看下一个排序键
                }
                return false;  // 所有键都相等
              });
    sorted_ = true;
  }
  // 排好序后，按下标依次返回
  if (idx_ >= records_.size()) {
    return nullptr;
  }
  return records_[idx_++];
}

}  // namespace huadb
