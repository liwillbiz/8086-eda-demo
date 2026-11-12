// B3 — Read data setup, TDVCL (brief §7 B): the tester's memory drives read data later and
// later into T3. The device must read correctly with the spec's minimum setup; a search
// finds the setup the die actually needs and logs it against TDVCL min.

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class B3DataSetup final : public TestCase {
 public:
  std::string_view id() const override { return "B3"; }
  std::string_view title() const override { return "Read data setup (TDVCL)"; }
  std::vector<std::string_view> spec_symbols() const override { return {"TDVCL"}; }

  Verdict run(TestContext& ctx) override {
    const double period = ctx.spec.grade(ctx.grade).tclcl_min_ns;
    const spec::Limit tdvcl = ctx.spec.ac("TDVCL", ctx.grade, period);
    // Read data is sampled at the CLK falling edge that ends T3 (the end of the period).
    const auto with_setup = [&](double setup) {
      tester::TimingSet ts = functional_timing(ctx.spec, ctx.grade);
      ts.pins["AD"].drive_ns = period - setup;
      try {
        ctx.t.set_timing(0, ts);
      } catch (const std::exception&) {
        return Outcome{false, "timing set rejected"};
      }
      return rom_outcome(ctx, "b1_checksum");
    };

    const Outcome at_spec = with_setup(*tdvcl.min);
    Verdict v = check(ctx, "pass_at_spec_setup", at_spec.pass ? 1 : 0,
                      exact(1, "brief §7 B3, setup = TDVCL min"), at_spec.why);
    const auto edge = find_edge([&](double s) { return with_setup(s).pass; }, period, 0.25, 0.25);
    if (!edge) return worst(v, missing(ctx, "data_setup_required", at_most_min(tdvcl), "fails with a full period of setup"));
    return worst(v, check(ctx, "data_setup_required", *edge, at_most_min(tdvcl), "search resolution 0.25 ns"));
  }
};
REGISTER_TEST(B3DataSetup);

}  // namespace
}  // namespace i8086::testprog
