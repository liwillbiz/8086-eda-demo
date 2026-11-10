#pragma once

// Layer-private: resolves a TimingSet (plus optional `.pat` timing overlay) into integer ps.

#include <cstdint>
#include <vector>

#include "i8086/spec/spec.hpp"
#include "i8086/tester/pattern.hpp"
#include "i8086/tester/tester.hpp"

namespace i8086::tester::detail {

struct PinEdges {
  DriveFormat fmt = DriveFormat::NRZ;
  int64_t drive_ps = 0;   // into the period
  int64_t strobe_ps = 0;  // into the period
};

// One CLK period starts at the CLK falling edge (start of a T-state), CLK rises
// `period_ps - high_ps` later and stays high until the next period.
struct ResolvedTiming {
  double period_ns = 0;
  int64_t period_ps = 0;
  int64_t high_ps = 0;
  std::vector<PinEdges> pins;  // indexed by spec pin number
};

int64_t to_ps(double ns);

// Spec-default CLK high time: the middle of the window that satisfies TCHCL.min and
// TCLCL - TCLCH.min for every grade at this period, or TCLCL/3 if that window is empty.
double default_clk_high_ns(const spec::Spec&, double period_ns);

void validate_timing_set(const spec::Spec&, const TimingSet&);  // throws std::invalid_argument
ResolvedTiming resolve_timing(const spec::Spec&, const TimingSet& base,
                              const PatternTiming* overlay);  // throws std::invalid_argument

}  // namespace i8086::tester::detail
