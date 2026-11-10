// D11 — Invalid opcode (brief §7 D, §5): 0Fh (POP CS) and D6h execute on the 8086 without
// a type-6 trap, unlike later chips (and unlike what the cheatsheet says).

#include <algorithm>

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

constexpr uint16_t kTrap6Marker = 0x0666;  // reported by the ROM's type-6 handler
constexpr uint16_t kRomSegment = 0xFF00;
constexpr uint16_t kStackTop = 0x1000;

class D11InvalidOpcode final : public RomCase {
 public:
  std::string_view id() const override { return "D11"; }
  std::string_view title() const override { return "Invalid opcode"; }

 protected:
  std::string_view rom_name() const override { return "d11_invalid_opcode"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto d = r.actual.data();
    Verdict v = check(ctx, "type6_entries", static_cast<double>(std::ranges::count(d, kTrap6Marker)),
                      exact(0, ref()));
    v = worst(v, expect_word(ctx, "CS_after_POP_CS", nth(d, 0), kRomSegment, ref()));
    v = worst(v, expect_word(ctx, "AL_after_D6_CF1", nth(d, 1), 0x00FF, ref()));
    v = worst(v, expect_word(ctx, "AL_after_D6_CF0", nth(d, 2), 0x0000, ref()));
    return worst(v, expect_word(ctx, "SP", nth(d, 3), kStackTop, ref()));
  }
};
REGISTER_TEST(D11InvalidOpcode);

}  // namespace
}  // namespace i8086::testprog
