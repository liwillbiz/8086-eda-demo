// C2 — VIL / VIH thresholds (brief §7 C): with every input (RESET, READY, INTR, NMI, HOLD,
// read data on AD) driven at exactly VIH min / VIL max the checksum loop runs correctly.
// Searches then move each drive level toward the other until the device misreads, which
// is the die's actual switching point.

#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class C2InputThresholds final : public TestCase {
 public:
  std::string_view id() const override { return "C2"; }
  std::string_view title() const override { return "VIL / VIH thresholds"; }
  std::vector<std::string_view> spec_symbols() const override { return {"VIL", "VIH"}; }

  Verdict run(TestContext& ctx) override {
    setup_functional(ctx);
    const spec::Limit vih = ctx.spec.dc("VIH", ctx.grade);
    const spec::Limit vil = ctx.spec.dc("VIL", ctx.grade);
    const auto with = [&](double high, double low) {
      tester::LevelSet levels;
      levels.vcc = ctx.vcc_v;
      levels.vih = high;
      levels.vil = low;
      try {
        ctx.t.set_levels(levels);
      } catch (const std::exception&) {
        return Outcome{false, "levels rejected"};
      }
      return rom_outcome(ctx, "b1_checksum");
    };

    const Outcome at_spec = with(*vih.min, *vil.max);
    Verdict v = check(ctx, "pass_at_vih_min_vil_max", at_spec.pass ? 1 : 0,
                      exact(1, std::format("brief §7 C2, drive {:g} V / {:g} V", *vih.min, *vil.max)), at_spec.why);

    const double res = 0.01;
    const auto high = find_edge([&](double h) { return with(h, *vil.max).pass; }, *vih.min, *vil.max, res);
    if (!high) {
      v = worst(v, missing(ctx, "vih_switch", at_most_min(vih), "fails at VIH min"));
    } else {
      v = worst(v, check(ctx, "vih_switch", *high, at_most_min(vih), "lowest passing high level"));
    }
    const auto low = find_edge([&](double l) { return with(*vih.min, l).pass; }, *vil.max, *vih.min, res);
    if (!low) return worst(v, missing(ctx, "vil_switch", at_least_max(vil), "fails at VIL max"));
    return worst(v, check(ctx, "vil_switch", *low, at_least_max(vil), "highest passing low level"));
  }
};
REGISTER_TEST(C2InputThresholds);

}  // namespace
}  // namespace i8086::testprog
