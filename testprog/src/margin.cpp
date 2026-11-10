#include "margin.hpp"

#include <exception>

#include "i8086/tester/search.hpp"

namespace i8086::testprog {

const std::vector<SuiteRom>& functional_suite() {
  static const std::vector<SuiteRom> suite{
      {"boot"},
      {"b1_checksum"},
      {"a3_byte_lanes"},
      {"d1_add_flags"},
      {"d2_inc_cf"},
      {"d3_divide_error", flag::kArithmetic},
      {"d4_segment_defaults"},
      {"d5_segment_wrap"},
      {"d6_rep_movsw_std"},
      {"d8_shift_unmasked", flag::AF | flag::OF},
      {"d9_flags_identity"},
      {"d11_invalid_opcode"},
  };
  return suite;
}

tester::RunOpts bounded_run(int wait_states, uint64_t max_cycles) {
  tester::RunOpts o;
  o.wait_states = wait_states;
  o.max_cycles = max_cycles;
  return o;
}

Outcome rom_outcome(TestContext& ctx, std::string_view rom, uint16_t undefined_flags,
                    const tester::RunOpts& opts) {
  try {
    const RomRun r = run_rom(ctx, rom, opts);
    if (!r.result.pass) return {false, std::string(rom) + ": did not halt"};
    DiffOptions o;
    o.undefined_flags = undefined_flags;
    o.max_messages = 1;
    if (const auto d = diff(r.expected, r.actual, o); !d.empty()) return {false, std::string(rom) + ": " + d.front()};
    if (!r.protocol.empty()) return {false, std::string(rom) + ": " + r.protocol.front()};
    return {true, {}};
  } catch (const std::exception& e) {
    return {false, std::string(rom) + ": " + e.what()};
  }
}

std::optional<double> find_edge(const std::function<bool(double)>& run_at, double pass_at,
                                double fail_at, double resolution) {
  try {
    return tester::search_pass_fail(run_at, pass_at, fail_at, resolution);
  } catch (const std::domain_error&) {
    return std::nullopt;
  }
}

spec::Limit at_most_min(spec::Limit l) {
  l.max = l.min;
  l.min.reset();
  return l;
}

spec::Limit at_least_max(spec::Limit l) {
  l.min = l.max;
  l.max.reset();
  return l;
}

double spec_clk_high(const spec::Spec& spec, spec::Grade g, double period_ns) {
  const double lo = spec.ac("TCHCL", g, period_ns).min.value_or(0);
  const double hi = period_ns - spec.ac("TCLCH", g, period_ns).min.value_or(0);
  return lo <= hi ? (lo + hi) / 2 : period_ns / 3;
}

spec::Limit info(std::string ref, std::string unit) {
  spec::Limit l;
  l.unit = std::move(unit);
  l.ref = std::move(ref);
  l.verified = true;
  return l;
}

Verdict missing(TestContext& ctx, std::string param, const spec::Limit& lim, std::string why) {
  ctx.log.record(std::move(param), std::nullopt, lim, Verdict::Fail, std::move(why));
  return Verdict::Fail;
}

}  // namespace i8086::testprog
