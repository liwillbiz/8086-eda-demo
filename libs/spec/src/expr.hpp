#pragma once

// Layer-private arithmetic expressions used for spec.yaml bounds such as "2*TCLCL-75".
// Grammar: expr := term (('+'|'-') term)*   term := unary (('*'|'/') unary)*
//          unary := '-' unary | primary     primary := number | name | name '.' (min|max) | '(' expr ')'

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace i8086::spec::detail {

class ExprError : public std::runtime_error {
 public:
  ExprError(const std::string& msg, std::size_t column)
      : std::runtime_error(msg), column_(column) {}
  std::size_t column() const { return column_; }  // 0-based offset into the expression text

 private:
  std::size_t column_;
};

enum class Bound { Min, Max };

struct Var {
  std::string name;
  std::optional<Bound> bound;  // set for "SYM.min" / "SYM.max"
  bool operator==(const Var&) const = default;
};

class Expr {
 public:
  using Resolver = std::function<double(const Var&)>;

  static Expr parse(std::string_view text);  // throws ExprError

  double eval(const Resolver&) const;  // throws ExprError on division by zero
  const std::vector<Var>& vars() const { return vars_; }
  bool is_constant() const { return vars_.empty(); }
  const std::string& text() const { return text_; }

  struct Node;

 private:
  std::shared_ptr<const Node> root_;
  std::vector<Var> vars_;
  std::string text_;
};

}  // namespace i8086::spec::detail
