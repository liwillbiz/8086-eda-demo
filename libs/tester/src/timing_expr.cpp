#include "timing_expr.hpp"

#include <cctype>
#include <charconv>
#include <cmath>

namespace i8086::tester::detail {
namespace {

class Parser {
 public:
  Parser(std::string_view s, const TimingExprEnv& env) : s_(s), env_(env) {}

  double parse() {
    const double v = expr();
    skip_ws();
    if (pos_ != s_.size()) fail("unexpected '" + std::string(1, s_[pos_]) + "'");
    return v;
  }

 private:
  [[noreturn]] void fail(const std::string& msg) const {
    throw TimingExprError("timing expression '" + std::string(s_) + "': " + msg);
  }

  void skip_ws() {
    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
  }

  bool eat(char c) {
    skip_ws();
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  double expr() {
    double v = term();
    for (;;) {
      if (eat('+')) {
        v += term();
      } else if (eat('-')) {
        v -= term();
      } else {
        return v;
      }
    }
  }

  double term() {
    double v = unary();
    for (;;) {
      if (eat('*')) {
        v *= unary();
      } else if (eat('/')) {
        const double d = unary();
        if (d == 0) fail("division by zero");
        v /= d;
      } else {
        return v;
      }
    }
  }

  double unary() {
    if (eat('-')) return -unary();
    return primary();
  }

  std::string_view ident() {
    const std::size_t start = pos_;
    while (pos_ < s_.size() &&
           (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) {
      ++pos_;
    }
    return s_.substr(start, pos_ - start);
  }

  double primary() {
    skip_ws();
    if (pos_ >= s_.size()) fail("unexpected end");
    if (eat('(')) {
      const double v = expr();
      if (!eat(')')) fail("missing ')'");
      return v;
    }
    const char c = s_[pos_];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return symbol();
    fail("unexpected '" + std::string(1, c) + "'");
  }

  double number() {
    double v = 0;
    const auto [end, ec] = std::from_chars(s_.data() + pos_, s_.data() + s_.size(), v);
    if (ec != std::errc{}) fail("bad number");
    pos_ = static_cast<std::size_t>(end - s_.data());
    const std::string_view unit = ident();
    if (unit.empty() || unit == "ns") return v;
    if (unit == "ps") return v / 1000.0;
    if (unit == "us") return v * 1000.0;
    fail("unknown unit '" + std::string(unit) + "' (use ns, ps or us)");
  }

  double symbol() {
    const std::string name(ident());
    if (pos_ < s_.size() && s_[pos_] == '.') {
      ++pos_;
      const std::string_view bound = ident();
      if (bound != "min" && bound != "max") fail("expected .min or .max after " + name);
      if (!env_.symbol) fail(name + "." + std::string(bound) + " needs a spec context");
      return env_.symbol(SymbolRef{name, bound == "max"});
    }
    if (name == "TCLCL") {
      if (!env_.tclcl_ns) fail("TCLCL is not known here");
      return *env_.tclcl_ns;
    }
    fail("'" + name + "' needs .min or .max");
  }

  std::string_view s_;
  const TimingExprEnv& env_;
  std::size_t pos_ = 0;
};

}  // namespace

double eval_timing_expr(std::string_view text, const TimingExprEnv& env) {
  const double v = Parser(text, env).parse();
  if (!std::isfinite(v)) throw TimingExprError("timing expression '" + std::string(text) + "' is not finite");
  return v;
}

}  // namespace i8086::tester::detail
