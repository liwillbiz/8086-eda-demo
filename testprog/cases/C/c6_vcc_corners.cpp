// C6 — VCC corners (brief §7 C): the functional suite (boot, checksum, A3 and the D ROMs)
// passes at the matrix point; the regression matrix puts it at 4.5 / 5.5 V and 0 / 70 °C.
// Every level applied stays inside the absolute maximum ratings.

#include <algorithm>
#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class C6VccCorners final : public TestCase {
 public:
  std::string_view id() const override { return "C6"; }
  std::string_view title() const override { return "VCC corners"; }

  Verdict run(TestContext& ctx) override {
    setup_functional(ctx);
    const std::string ref = "brief §7 C6";
    int failed = 0;
    std::string first;
    for (const SuiteRom& r : functional_suite()) {
      const Outcome o = rom_outcome(ctx, r.name, r.undefined_flags);
      if (o.pass) continue;
      if (failed++ == 0) first = o.why;
    }
    Verdict v = check(ctx, "suite_roms_failed", failed, exact(0, ref),
                      failed ? first : std::format("{} ROMs", functional_suite().size()));

    const tester::LevelSet levels;
    const double highest = std::max({ctx.vcc_v, levels.vih, levels.vch});
    v = worst(v, check(ctx, "highest_applied_v", highest, ctx.spec.abs_max("V_PIN"), "VCC, VIH, VCH"));
    return worst(v, check(ctx, "ta_under_bias", ctx.temp_c, ctx.spec.abs_max("TA_BIAS")));
  }
};
REGISTER_TEST(C6VccCorners);

}  // namespace
}  // namespace i8086::testprog
