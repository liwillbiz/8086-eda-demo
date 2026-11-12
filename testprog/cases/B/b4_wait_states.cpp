// B4 — Wait-state insertion (brief §7 B): with READY low for N clocks every bus cycle has
// exactly N Tw states, for N = 1..3. READY returns high exactly TRYHCH min before the CLK
// rising edge that samples it; a search finds the setup the die actually needs.

#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

using model::BusCycleType;

class B4WaitStates final : public TestCase {
 public:
  std::string_view id() const override { return "B4"; }
  std::string_view title() const override { return "Wait-state insertion"; }
  std::vector<std::string_view> spec_symbols() const override { return {"TRYHCH", "TCHRYX"}; }

  Verdict run(TestContext& ctx) override {
    const double period = ctx.spec.grade(ctx.grade).tclcl_min_ns;
    const double high = spec_clk_high(ctx.spec, ctx.grade, period);
    const double rise = period - high;  // CLK rises this far into the period
    const spec::Limit tryhch = ctx.spec.ac("TRYHCH", ctx.grade, period);
    const spec::Limit tchryx = ctx.spec.ac("TCHRYX", ctx.grade, period);
    const std::string ref = "brief §7 B4";

    struct Run {
      Outcome outcome;
      int cycles = 0, wrong_tw = 0;
    };
    // READY is driven at `rise - setup` in T2 (low) and in the last Tw (high again).
    const auto run_with = [&](int n, double setup) {
      tester::TimingSet ts = functional_timing(ctx.spec, ctx.grade);
      ts.clk_high_ns = high;
      ts.pins["READY"].drive_ns = rise - setup;
      Run r;
      try {
        ctx.t.set_timing(0, ts);
      } catch (const std::exception&) {
        r.outcome = {false, "timing set rejected"};
        return r;
      }
      r.outcome = rom_outcome(ctx, "b1_checksum", 0, bounded_run(n));
      for (const auto& tx : ctx.t.last_trace().transactions) {
        if (tx.type == BusCycleType::None || tx.type == BusCycleType::Halt) continue;
        ++r.cycles;
        if (tx.wait_states != n) ++r.wrong_tw;
      }
      return r;
    };

    Verdict v = Verdict::Pass;
    for (int n = 1; n <= 3; ++n) {
      const Run r = run_with(n, *tryhch.min);
      v = worst(v, check(ctx, std::format("program_pass_tw{}", n), r.outcome.pass ? 1 : 0, exact(1, ref),
                         r.outcome.why));
      v = worst(v, check(ctx, std::format("cycles_without_{}_tw", n), r.wrong_tw,
                         exact(0, ref + ": exactly N Tw"), std::format("{} bus cycles", r.cycles)));
    }

    // Hold after the edge that samples READY low: from that rising edge to READY's next
    // drive edge, one period later.
    v = worst(v, check(ctx, "ready_hold_applied", high + rise - *tryhch.min, tchryx,
                       "tester-applied; one READY edge per period cannot shrink it independently"));

    const auto edge = find_edge(
        [&](double s) {
          const Run r = run_with(1, s);
          return r.outcome.pass && r.wrong_tw == 0;
        },
        rise, rise - period + 0.25, 0.25);
    if (!edge) return worst(v, missing(ctx, "ready_setup_required", at_most_min(tryhch), "fails with READY driven at the period start"));
    return worst(v, check(ctx, "ready_setup_required", *edge, at_most_min(tryhch), "search resolution 0.25 ns"));
  }
};
REGISTER_TEST(B4WaitStates);

}  // namespace
}  // namespace i8086::testprog
