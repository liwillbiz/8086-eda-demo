#pragma once

// Layer-private evaluator for `.pat` timing attributes such as "TCLAV.max+5ns".
// Grammar: expr := term (('+'|'-') term)*   term := unary (('*'|'/') unary)*
//          unary := '-' unary | primary
//          primary := number [ns|ps|us] | TCLCL | SYM '.' (min|max) | '(' expr ')'
// Plain numbers are ns. TCLCL is the period of the timing set being resolved.

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace i8086::tester::detail {

class TimingExprError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct SymbolRef {
  std::string name;  // spec AC symbol
  bool max = false;  // false = .min
};

struct TimingExprEnv {
  std::optional<double> tclcl_ns;
  std::function<double(const SymbolRef&)> symbol;  // spec bound in ns; may throw
};

double eval_timing_expr(std::string_view text, const TimingExprEnv&);  // ns; throws TimingExprError

}  // namespace i8086::tester::detail
