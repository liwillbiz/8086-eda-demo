// D6 — REP string with DF = 1 (brief §7 D): STD; REP MOVSW with CX = 4 copies four words
// downward, leaves SI and DI 8 lower and CX = 0.

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class D6RepMovswStd final : public RomCase {
 public:
  std::string_view id() const override { return "D6"; }
  std::string_view title() const override { return "REP string + DF"; }

 protected:
  std::string_view rom_name() const override { return "d6_rep_movsw_std"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto d = r.actual.data();
    Verdict v = expect_word(ctx, "CX", nth(d, 0), 0, ref());
    v = worst(v, expect_word(ctx, "SI", nth(d, 1), 0x0106 - 8, ref()));
    v = worst(v, expect_word(ctx, "DI", nth(d, 2), 0x0206 - 8, ref()));
    static constexpr uint16_t kCopied[] = {0x1111, 0x2222, 0x3333, 0x4444};
    for (std::size_t i = 0; i < 4; ++i) {
      v = worst(v, expect_word(ctx, "dest_word" + std::to_string(i), nth(d, 3 + i), kCopied[i], ref()));
    }
    return worst(v, expect_word(ctx, "dest_guard", nth(d, 7), 0xEEEE, ref()));
  }
};
REGISTER_TEST(D6RepMovswStd);

}  // namespace
}  // namespace i8086::testprog
