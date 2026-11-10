// B2 — Clock duty-cycle margin (brief §7 B): at the grade's fastest clock the device works
// with CLK high for exactly TCHCL min and low for exactly TCLCH min. Searches then shrink
// each phase until the checksum loop fails, so the datalog shows how far inside the
// limits the die's own failing edge is.

#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class B2ClockDuty final : public TestCase {
 public:
  std::string_view id() const override { return "B2"; }
  std::string_view title() const override { return "Clock duty-cycle margin"; }
  std::vector<std::string_view> spec_symbols() const override { return {"TCHCL", "TCLCH"}; }

  Verdict run(TestContext& ctx) override {
    const double period = ctx.spec.grade(ctx.grade).tclcl_min_ns;
    const spec::Limit tchcl = ctx.spec.ac("TCHCL", ctx.grade, period);
    const spec::Limit tclch = ctx.spec.ac("TCLCH", ctx.grade, period);
    const auto with_high = [&](double high) {
      tester::TimingSet ts = functional_timing(ctx.spec, ctx.grade);
      ts.clk_high_ns = high;
      try {
        ctx.t.set_timing(0, ts);
      } catch (const std::exception&) {
        return Outcome{false, "timing set rejected"};
      }
      return rom_outcome(ctx, "b1_checksum");
    };
    const std::string ref = "brief §7 B2";

    Verdict v = Verdict::Pass;
    Outcome o = with_high(*tchcl.min);
    v = worst(v, check(ctx, "pass_at_min_high", o.pass ? 1 : 0, exact(1, ref + ", CLK high = TCHCL min"), o.why));
    o = with_high(period - *tclch.min);
    v = worst(v, check(ctx, "pass_at_min_low", o.pass ? 1 : 0, exact(1, ref + ", CLK low = TCLCH min"), o.why));

    const double res = 0.5;
    const auto high = find_edge([&](double h) { return with_high(h).pass; }, *tchcl.min, res, res);
    if (!high) {
      v = worst(v, missing(ctx, "clk_high_shortest_pass", at_most_min(tchcl), "fails at TCHCL min"));
    } else {
      v = worst(v, check(ctx, "clk_high_shortest_pass", *high, at_most_min(tchcl)));
    }
    const auto low = find_edge([&](double l) { return with_high(period - l).pass; }, *tclch.min, res, res);
    if (!low) {
      return worst(v, missing(ctx, "clk_low_shortest_pass", at_most_min(tclch), "fails at TCLCH min"));
    }
    return worst(v, check(ctx, "clk_low_shortest_pass", *low, at_most_min(tclch)));
  }
};
REGISTER_TEST(B2ClockDuty);

}  // namespace
}  // namespace i8086::testprog
