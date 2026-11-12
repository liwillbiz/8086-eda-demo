#include "expr.hpp"

#include <cctype>
#include <charconv>

namespace i8086::spec::detail {

struct Expr::Node {
  enum class Kind { Num, Var, Neg, Add, Sub, Mul, Div } kind;
  double num = 0;
  Var var;
  std::shared_ptr<const Node> lhs, rhs;
  std::size_t column = 0;
};

namespace {

using NodeP = std::shared_ptr<const Expr::Node>;

class Parser {
 public:
  Parser(std::string_view s, std::vector<Var>& vars) : s_(s), vars_(vars) {}

  NodeP parse() {
    NodeP n = expr();
    skip_ws();
    if (pos_ != s_.size()) throw ExprError("unexpected '" + std::string(1, s_[pos_]) + "'", pos_);
    return n;
  }

 private:
  NodeP expr() {
    NodeP n = term();
    for (;;) {
      skip_ws();
      if (eat('+')) {
        n = binary(Expr::Node::Kind::Add, n, term());
      } else if (eat('-')) {
        n = binary(Expr::Node::Kind::Sub, n, term());
      } else {
        return n;
      }
    }
  }

  NodeP term() {
    NodeP n = unary();
    for (;;) {
      skip_ws();
      const std::size_t at = pos_;
      if (eat('*')) {
        n = binary(Expr::Node::Kind::Mul, n, unary());
      } else if (eat('/')) {
        n = binary(Expr::Node::Kind::Div, n, unary(), at);
      } else {
        return n;
      }
    }
  }

  NodeP unary() {
    skip_ws();
    if (eat('-')) {
      auto node = std::make_shared<Expr::Node>();
      node->kind = Expr::Node::Kind::Neg;
      node->lhs = unary();
      return node;
    }
    return primary();
  }

  NodeP primary() {
    skip_ws();
    if (pos_ == s_.size()) throw ExprError("unexpected end of expression", pos_);
    const char c = s_[pos_];
    if (eat('(')) {
      NodeP n = expr();
      skip_ws();
      if (!eat(')')) throw ExprError("expected ')'", pos_);
      return n;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return variable();
    throw ExprError("unexpected '" + std::string(1, c) + "'", pos_);
  }

  NodeP number() {
    const std::size_t start = pos_;
    while (pos_ < s_.size() &&
           (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.')) {
      ++pos_;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    auto node = std::make_shared<Expr::Node>();
    node->kind = Expr::Node::Kind::Num;
    const char* first = s_.data() + start;
    const char* last = s_.data() + pos_;
    const auto [ptr, ec] = std::from_chars(first, last, node->num);
    if (ec != std::errc() || ptr != last) throw ExprError("invalid number", start);
    return node;
  }

  NodeP variable() {
    const std::size_t start = pos_;
    auto ident = [&] {
      const std::size_t b = pos_;
      while (pos_ < s_.size() &&
             (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) {
        ++pos_;
      }
      return std::string(s_.substr(b, pos_ - b));
    };
    auto node = std::make_shared<Expr::Node>();
    node->kind = Expr::Node::Kind::Var;
    node->column = start;
    node->var.name = ident();
    if (eat('.')) {
      const std::size_t at = pos_;
      const std::string b = ident();
      if (b == "min") {
        node->var.bound = Bound::Min;
      } else if (b == "max") {
        node->var.bound = Bound::Max;
      } else {
        throw ExprError("expected 'min' or 'max' after '.'", at);
      }
    }
    vars_.push_back(node->var);
    return node;
  }

  static NodeP binary(Expr::Node::Kind k, NodeP l, NodeP r, std::size_t column = 0) {
    auto node = std::make_shared<Expr::Node>();
    node->kind = k;
    node->column = column;
    node->lhs = std::move(l);
    node->rhs = std::move(r);
    return node;
  }

  void skip_ws() {
    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
  }
  bool eat(char c) {
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  std::string_view s_;
  std::vector<Var>& vars_;
  std::size_t pos_ = 0;
};

double eval_node(const Expr::Node& n, const Expr::Resolver& resolve) {
  using K = Expr::Node::Kind;
  switch (n.kind) {
    case K::Num: return n.num;
    case K::Var: return resolve(n.var);
    case K::Neg: return -eval_node(*n.lhs, resolve);
    case K::Add: return eval_node(*n.lhs, resolve) + eval_node(*n.rhs, resolve);
    case K::Sub: return eval_node(*n.lhs, resolve) - eval_node(*n.rhs, resolve);
    case K::Mul: return eval_node(*n.lhs, resolve) * eval_node(*n.rhs, resolve);
    case K::Div: {
      const double d = eval_node(*n.rhs, resolve);
      if (d == 0) throw ExprError("division by zero", n.column);
      return eval_node(*n.lhs, resolve) / d;
    }
  }
  return 0;
}

}  // namespace

Expr Expr::parse(std::string_view text) {
  Expr e;
  e.text_ = std::string(text);
  e.root_ = Parser(e.text_, e.vars_).parse();
  return e;
}

double Expr::eval(const Resolver& resolve) const { return eval_node(*root_, resolve); }

}  // namespace i8086::spec::detail
