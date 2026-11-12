// C3 — Input leakage (brief §7 C): 0 V and VCC on each input pin; |ILI| <= 10 µA.

#include <cmath>
#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class C3InputLeakage final : public TestCase {
 public:
  std::string_view id() const override { return "C3"; }
  std::string_view title() const override { return "Input leakage"; }
  std::vector<std::string_view> spec_symbols() const override { return {"ILI"}; }

  Verdict run(TestContext& ctx) override {
    const spec::Limit ili = ctx.spec.dc("ILI", ctx.grade);
    Verdict v = Verdict::Pass;
    for (double volts : {0.0, ctx.vcc_v}) {
      double worst_ua = 0;
      std::string worst_pin;
      int n = 0;
      for (const auto& p : ctx.spec.pins()) {
        if (p.dir != spec::Dir::In) continue;
        const double ua = ctx.t.force_v_measure_i(p.name, volts);
        ++n;
        if (worst_pin.empty() || std::abs(ua) > std::abs(worst_ua)) {
          worst_ua = ua;
          worst_pin = p.name;
        }
      }
      v = worst(v, check(ctx, volts == 0 ? "ILI_at_0V" : "ILI_at_VCC", worst_ua, ili,
                         std::format("worst pin {} of {} inputs", worst_pin, n)));
    }
    return v;
  }
};
REGISTER_TEST(C3InputLeakage);

}  // namespace
}  // namespace i8086::testprog
