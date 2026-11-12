// D10 — Single step (brief §7 D, §5): POPF sets TF, and each of the next three instructions
// ends in a type-1 trap. TF is clear inside the handler; the third entry clears TF in the
// stacked FLAGS, so stepping stops after its IRET.

#include "interrupts.hpp"
#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class D10SingleStep final : public RomCase {
 public:
  std::string_view id() const override { return "D10"; }
  std::string_view title() const override { return "Single step (TF)"; }

 protected:
  std::string_view rom_name() const override { return "d10_single_step"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto d = r.actual.data();
    const auto f = r.actual.flags();
    Verdict v = check(ctx, "type1_entries", static_cast<double>(vector_fetches(ctx.t.last_trace(), 1)), exact(3, ref()));
    v = worst(v, expect_word(ctx, "handler_count", nth(d, 3), 3, ref()));
    v = worst(v, expect_word(ctx, "stacked_IP_1", nth(d, 0), nth(d, 4).value_or(0), ref()));
    v = worst(v, expect_word(ctx, "stacked_IP_2", nth(d, 1), nth(d, 5).value_or(0), ref()));
    v = worst(v, expect_word(ctx, "stacked_IP_3", nth(d, 2), nth(d, 6).value_or(0), ref()));
    for (std::size_t i = 0; i < 3; ++i) {
      v = worst(v, expect_word(ctx, "TF_in_handler_" + std::to_string(i + 1), nth(f, i), 0, "brief §5", flag::TF));
    }
    return worst(v, expect_word(ctx, "TF_after_stepping", nth(f, 3), 0, ref(), flag::TF));
  }
};
REGISTER_TEST(D10SingleStep);

}  // namespace
}  // namespace i8086::testprog
