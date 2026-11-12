// C5 — ICC (brief §7 C): run the checksum loop at the grade's maximum frequency, then
// measure supply current; <= 340 / 350 / 360 mA by grade. Measured at the matrix point's
// VCC and temperature (the brief's 25 °C is the shmoo and bring-up point).

#include <format>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

class C5Icc final : public TestCase {
 public:
  std::string_view id() const override { return "C5"; }
  std::string_view title() const override { return "ICC"; }
  std::vector<std::string_view> spec_symbols() const override { return {"ICC"}; }

  Verdict run(TestContext& ctx) override {
    setup_functional(ctx);
    const Outcome o = rom_outcome(ctx, "b1_checksum");
    Verdict v = check(ctx, "program_pass", o.pass ? 1 : 0, exact(1, "brief §7 C5"), o.why);
    return worst(v, check(ctx, "ICC", ctx.t.measure_icc(), ctx.spec.dc("ICC", ctx.grade),
                          std::format("{:g} MHz", ctx.spec.grade(ctx.grade).fclk_max_mhz)));
  }
};
REGISTER_TEST(C5Icc);

}  // namespace
}  // namespace i8086::testprog
