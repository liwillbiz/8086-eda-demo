// B1 — Fmax / Fmin per grade (brief §7 B): the checksum loop runs correctly from TCLCL
// 500 ns down to the grade minimum. A search then finds the shortest passing period, so
// the datalog shows the die's Fmax and its margin to the grade.

#include <algorithm>
#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class B1Fmax final : public TestCase {
 public:
  std::string_view id() const override { return "B1"; }
  std::string_view title() const override { return "Fmax / Fmin per grade"; }
  std::vector<std::string_view> spec_symbols() const override { return {"TCLCL"}; }

  Verdict run(TestContext& ctx) override {
    const spec::GradeInfo& g = ctx.spec.grade(ctx.grade);
    const std::string ref = "brief §7 B1; spec grades TCLCL";
    const auto passes_at = [&](double period) {
      tester::TimingSet ts = functional_timing(ctx.spec, ctx.grade);
      ts.period_ns = period;
      try {
        ctx.t.set_timing(0, ts);
      } catch (const std::exception&) {
        return Outcome{false, "timing set rejected"};
      }
      return rom_outcome(ctx, "b1_checksum");
    };

    std::vector<double> periods{g.tclcl_max_ns, g.tclcl_min_ns};
    for (double p : {400.0, 300.0, 250.0, 200.0, 150.0, 125.0}) {
      if (p < g.tclcl_max_ns && p > g.tclcl_min_ns) periods.push_back(p);
    }
    std::ranges::sort(periods, std::greater<>());

    Verdict v = Verdict::Pass;
    for (double p : periods) {
      const Outcome o = passes_at(p);
      v = worst(v, check(ctx, std::format("checksum_at_{:g}ns", p), o.pass ? 1 : 0, exact(1, ref), o.why));
    }

    spec::Limit fmax;
    fmax.max = g.tclcl_min_ns;
    fmax.unit = "ns";
    fmax.ref = ref + " min (shortest passing period must not exceed it)";
    fmax.verified = true;
    const auto edge = find_edge([&](double p) { return passes_at(p).pass; }, g.tclcl_min_ns, 40.0, 0.5);
    if (!edge) return worst(v, missing(ctx, "tclcl_shortest_pass", fmax, "fails at the grade's TCLCL min"));
    return worst(v, check(ctx, "tclcl_shortest_pass", *edge, fmax,
                          std::format("Fmax {:.2f} MHz (grade {:g} MHz)", 1000.0 / *edge, g.fclk_max_mhz)));
  }
};
REGISTER_TEST(B1Fmax);

}  // namespace
}  // namespace i8086::testprog
