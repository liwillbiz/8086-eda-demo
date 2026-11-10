// C4 — 3-state leakage (brief §7 C): float the outputs by holding RESET (reset_float.pat),
// then force 0.45 V and VCC on each 3-state pin; |ILO| <= 10 µA. A 3-state pin the device
// still drives during RESET is counted and fails the test.

#include <cmath>
#include <format>
#include <stdexcept>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

constexpr double kVoutLow = 0.45;  // spec ILO condition: VOUT 0.45 V to VCC

class C4TristateLeakage final : public TestCase {
 public:
  std::string_view id() const override { return "C4"; }
  std::string_view title() const override { return "3-state leakage"; }
  std::vector<std::string_view> spec_symbols() const override { return {"ILO"}; }

  Verdict run(TestContext& ctx) override {
    setup_functional(ctx);
    ctx.t.run_pattern(pattern("reset_float"));
    const spec::Limit ilo = ctx.spec.dc("ILO", ctx.grade);
    const std::string ref = "brief §7 C4";

    Verdict v = Verdict::Pass;
    std::string driven;
    int n_driven = 0;
    for (double volts : {kVoutLow, ctx.vcc_v}) {
      double worst_ua = 0;
      std::string worst_pin;
      int n = 0;
      for (const auto& p : ctx.spec.pins()) {
        if (!p.tristate) continue;
        try {
          const double ua = ctx.t.force_v_measure_i(p.name, volts);
          ++n;
          if (worst_pin.empty() || std::abs(ua) > std::abs(worst_ua)) {
            worst_ua = ua;
            worst_pin = p.name;
          }
        } catch (const std::domain_error&) {
          if (volts == kVoutLow) {
            ++n_driven;
            driven += (driven.empty() ? "" : " ") + p.name;
          }
        }
      }
      const std::string param = volts == kVoutLow ? "ILO_at_0.45V" : "ILO_at_VCC";
      if (n == 0) {
        v = worst(v, missing(ctx, param, ilo, "no 3-state pin floated"));
      } else {
        v = worst(v, check(ctx, param, worst_ua, ilo, std::format("worst pin {} of {} floated", worst_pin, n)));
      }
    }
    return worst(v, check(ctx, "tristate_pins_driven_in_reset", n_driven, exact(0, ref + ", brief §5 RESET"), driven));
  }
};
REGISTER_TEST(C4TristateLeakage);

}  // namespace
}  // namespace i8086::testprog
