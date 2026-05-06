#pragma once

#include <memory>
#include <string>

#include "binder/expression.h"
#include "fmt/format.h"

namespace huadb {

class UnaryOpExpression : public Expression {
 public:
  UnaryOpExpression(std::string op_name, std::unique_ptr<Expression> arg)
      : Expression(ExpressionType::UNARY_OP), op_name_(std::move(op_name)), arg_(std::move(arg)) {}
  int Precedence() const override { return OpPrecedence(op_name_); }
  std::string ToString() const override {
    int my_prec = Precedence();
    std::string a = arg_ ? arg_->ToStringWithParens(my_prec) : "";
    return fmt::format("{} {}", op_name_, a);
  }
  bool HasAggregation() const override { return arg_->HasAggregation(); }

  std::string op_name_;
  std::unique_ptr<Expression> arg_;
};

}  // namespace huadb
