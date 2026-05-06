#pragma once

#include <string>

#include "common/exceptions.h"
#include "fmt/format.h"

namespace huadb {

enum class ExpressionType {
  AGGREGATE,
  ALIAS,
  BINARY_OP,
  TYPE_CAST,
  COLUMN_REF,
  CONST,
  FUNC_CALL,
  INVALID,
  LIST,
  NULL_TEST,
  STAR,
  UNARY_OP,
};

// Precedence levels used by operator-aware ToString printing. Higher
// number = binds tighter. Leaves (column ref / const / function call /
// list / star / ...) get the maximum value so they never trigger
// parens when used as a child.
//
// Convention follows SQL standard: `not` is weaker than comparison
// (`not a = 1` ≡ `not (a = 1)`), but stronger than `and`/`or`.
inline int OpPrecedence(const std::string &op) {
  if (op == "or") return 1;
  if (op == "and") return 2;
  if (op == "not") return 3;
  if (op == "=" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
  if (op == "+" || op == "-") return 5;
  if (op == "*" || op == "/" || op == "%") return 6;
  return 100;
}

class Expression {
 public:
  Expression() : type_(ExpressionType::INVALID) {}
  explicit Expression(ExpressionType type) : type_(type) {}
  virtual ~Expression() = default;

  virtual std::string ToString() const { return ""; }
  virtual bool HasAggregation() const { throw DbException("HasAggregation not implemented"); }
  // Operator precedence used to decide whether this expression needs
  // parens when printed as a child of another operator. Default for
  // non-operator expressions (column refs, constants, …) is the
  // maximum so they never get wrapped.
  virtual int Precedence() const { return 100; }
  // Render this expression, surrounding it with parens iff its top
  // operator binds less tightly than the parent's.
  std::string ToStringWithParens(int parent_prec) const {
    if (Precedence() < parent_prec) {
      return "(" + ToString() + ")";
    }
    return ToString();
  }
  ExpressionType type_;
};

}  // namespace huadb

template <typename T>
struct fmt::formatter<std::unique_ptr<T>, std::enable_if_t<std::is_base_of<huadb::Expression, T>::value, char>>
    : fmt::formatter<std::string> {
  auto format(const std::unique_ptr<T> &expr, format_context &ctx) const {
    if (!expr) {
      return fmt::formatter<std::string>::format("", ctx);
    }
    return fmt::formatter<std::string>::format(expr->ToString(), ctx);
  }
};

template <typename T>
struct fmt::formatter<std::shared_ptr<T>, std::enable_if_t<std::is_base_of<huadb::Expression, T>::value, char>>
    : fmt::formatter<std::string> {
  auto format(const std::shared_ptr<T> &expr, format_context &ctx) const {
    if (!expr) {
      return fmt::formatter<std::string>::format("", ctx);
    }
    return fmt::formatter<std::string>::format(expr->ToString(), ctx);
  }
};
