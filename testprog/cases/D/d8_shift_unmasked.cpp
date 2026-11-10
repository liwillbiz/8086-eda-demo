// D8 — Shift count not masked (brief §7 D, §2): MOV CL,33; SHL AX,CL gives AX = 0 on the
// 8086 (a 286 or later masks the count to 5 bits). AF, and OF for a count other than 1,
// are undefined after a shift.

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class D8ShiftUnmasked final : public RomCase {
 public:
  std::string_view id() const override { return "D8"; }
  std::string_view title() const override { return "Shift count unmasked"; }

 protected:
  std::string_view rom_name() const override { return "d8_shift_unmasked"; }
  DiffOptions diff_options() const override {
    DiffOptions o;
    o.undefined_flags = flag::AF | flag::OF;
    return o;
  }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto d = r.actual.data();
    Verdict v = expect_word(ctx, "AX_after_SHL_33", nth(d, 0), 0x0000, ref());
    v = worst(v, expect_word(ctx, "AX_after_SHR_17", nth(d, 1), 0x0000, ref()));
    return worst(v, expect_word(ctx, "AX_after_SHL_1", nth(d, 2), 0x2468, ref()));
  }
};
REGISTER_TEST(D8ShiftUnmasked);

}  // namespace
}  // namespace i8086::testprog
