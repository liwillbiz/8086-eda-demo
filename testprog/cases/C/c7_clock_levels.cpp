// C7 — Clock input levels (brief §7 C): with CLK driven at exactly VCH min / VCL max the
// checksum loop runs correctly. Searches then move each CLK level toward the other until
// the device stops clocking, which is its CLK switching point.

#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class C7ClockLevels final : public TestCase {
 public:
  std::string_view id() const override { return "C7"; }
  std::string_view title() const override { return "Clock input levels"; }
  std::vector<std::string_view> spec_symbols() const override { return {"VCH", "VCL"}; }

  Verdict run(TestContext& ctx) override {
    setup_functional(ctx);
    const spec::Limit vch = ctx.spec.dc("VCH", ctx.grade);
    const spec::Limit vcl = ctx.spec.dc("VCL", ctx.grade);
    const auto with = [&](double high, double low) {
      tester::LevelSet levels;
      levels.vcc = ctx.vcc_v;
      levels.vch = high;
      levels.vcl = low;
      try {
        ctx.t.set_levels(levels);
      } catch (const std::exception&) {
        return Outcome{false, "levels rejected"};
      }
      return rom_outcome(ctx, "b1_checksum");
    };

    const Outcome at_spec = with(*vch.min, *vcl.max);
    Verdict v = check(ctx, "pass_at_vch_min_vcl_max", at_spec.pass ? 1 : 0,
                      exact(1, std::format("brief §7 C7, CLK {:g} V / {:g} V", *vch.min, *vcl.max)), at_spec.why);

    const double res = 0.01;
    const auto high = find_edge([&](double h) { return with(h, *vcl.max).pass; }, *vch.min, *vcl.max, res);
    if (!high) {
      v = worst(v, missing(ctx, "vch_switch", at_most_min(vch), "fails at VCH min"));
    } else {
      v = worst(v, check(ctx, "vch_switch", *high, at_most_min(vch), "lowest passing CLK high level"));
    }
    const auto low = find_edge([&](double l) { return with(*vch.min, l).pass; }, *vcl.max, *vch.min, res);
    if (!low) return worst(v, missing(ctx, "vcl_switch", at_least_max(vcl), "fails at VCL max"));
    return worst(v, check(ctx, "vcl_switch", *low, at_least_max(vcl), "highest passing CLK low level"));
  }
};
REGISTER_TEST(C7ClockLevels);

}  // namespace
}  // namespace i8086::testprog
