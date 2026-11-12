#pragma once

// FROZEN INTERFACE (M0).

#include <cmath>
#include <stdexcept>

namespace i8086::tester {

// Binary search for the pass/fail boundary of `run_at(x) -> bool pass` on [lo, hi].
// Requires run_at(lo) == pass (lo may be greater than hi, to search downward).
// Returns the last passing value within `resolution` of the boundary, or hi if hi passes.
template <class F>
double search_pass_fail(F run_at, double lo, double hi, double resolution) {
  if (!(resolution > 0)) throw std::invalid_argument("search_pass_fail: resolution must be > 0");
  if (!run_at(lo)) throw std::domain_error("search_pass_fail: fails at start point");
  if (run_at(hi)) return hi;
  double pass = lo, fail = hi;
  while (std::abs(fail - pass) > resolution) {
    const double mid = pass + (fail - pass) / 2;
    (run_at(mid) ? pass : fail) = mid;
  }
  return pass;
}

}  // namespace i8086::tester
