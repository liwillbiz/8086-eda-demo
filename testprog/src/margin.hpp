#pragma once

// Layer-private helpers for the B (timing) and C (electrical) margin tests and the shmoo:
// a pass/fail predicate over one functional ROM run, and searches built on it.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "functional.hpp"
#include "rom_case.hpp"

namespace i8086::testprog {

// A ROM and the FLAGS bits its diff ignores. functional_suite() is boot, the B1 checksum,
// A3 and every D ROM with the masks their D cases use.
struct SuiteRom {
  std::string_view name;
  uint16_t undefined_flags = 0;
};
const std::vector<SuiteRom>& functional_suite();

struct Outcome {
  bool pass = false;
  std::string why;  // empty on pass
};

// RunOpts that give up after `max_cycles` CLKs, so a broken run (a stalled clock, a
// runaway program) fails fast inside a search.
tester::RunOpts bounded_run(int wait_states = 0, uint64_t max_cycles = 10'000);

// Runs one ROM at the tester's current levels and timing set 0: it must halt, match the
// golden emulator and break no protocol rule. Exceptions (an abs-max guard, a bad timing
// set) count as a fail, so searches can step outside the legal range.
Outcome rom_outcome(TestContext&, std::string_view rom, uint16_t undefined_flags = 0,
                    const tester::RunOpts& = bounded_run());

// tester::search_pass_fail, but nullopt when `pass_at` already fails.
std::optional<double> find_edge(const std::function<bool(double)>& run_at, double pass_at,
                                double fail_at, double resolution);

// A limit turned around for a measured requirement: a die that needs at most the spec's
// guaranteed minimum (setup, pulse) passes. Keeps unit, provenance and verified.
spec::Limit at_most_min(spec::Limit);
spec::Limit at_least_max(spec::Limit);
// CLK high time in the middle of the grade's TCHCL / TCLCH window at `period_ns`.
double spec_clk_high(const spec::Spec&, spec::Grade, double period_ns);

// A logged value that is not judged.
spec::Limit info(std::string ref, std::string unit = "");

// Logs `param` as unmeasured and returns Fail.
Verdict missing(TestContext&, std::string param, const spec::Limit&, std::string why);

}  // namespace i8086::testprog
