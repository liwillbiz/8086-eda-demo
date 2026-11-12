// D3 — Divide error (brief §7 D, §5): MOV AX,1; MOV BL,0; DIV BL raises type 0, and the
// 8086 stacks the IP of the instruction after the DIV. DIV leaves the arithmetic flags
// undefined, so they are masked in the golden diff.

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

constexpr uint16_t kRomSegment = 0xFF00;  // testprog/roms/inc/i86rom.inc ROM_SEG

class D3DivideError final : public RomCase {
 public:
  std::string_view id() const override { return "D3"; }
  std::string_view title() const override { return "Divide error"; }

 protected:
  std::string_view rom_name() const override { return "d3_divide_error"; }
  DiffOptions diff_options() const override {
    DiffOptions o;
    o.undefined_flags = flag::kArithmetic;
    return o;
  }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto d = r.actual.data();
    Verdict v = check(ctx, "handler_reports", static_cast<double>(d.size()), exact(3, ref()),
                      d.size() == 3 ? "" : "type-0 handler not entered exactly once");
    v = worst(v, expect_word(ctx, "stacked_IP", nth(d, 0), nth(d, 2).value_or(0), ref() + " (IP after DIV)"));
    v = worst(v, expect_word(ctx, "stacked_CS", nth(d, 1), kRomSegment, ref()));
    return worst(v, expect_word(ctx, "IF_TF_in_handler", nth(r.actual.flags(), 1), 0, "brief §5",
                                flag::IF | flag::TF));
  }
};
REGISTER_TEST(D3DivideError);

}  // namespace
}  // namespace i8086::testprog
