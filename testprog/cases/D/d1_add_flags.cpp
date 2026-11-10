// D1 — ADD flags (brief §7 D): MOV AL,7Fh; ADD AL,1 gives AL = 80h, OF = SF = AF = 1,
// ZF = PF = CF = 0. The ROM adds a byte carry and a word overflow case for the golden diff.

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class D1AddFlags final : public RomCase {
 public:
  std::string_view id() const override { return "D1"; }
  std::string_view title() const override { return "ADD flags"; }

 protected:
  std::string_view rom_name() const override { return "d1_add_flags"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    using namespace flag;
    const Verdict v = expect_word(ctx, "AL", nth(r.actual.data(), 0), 0x80, ref(), 0x00FF);
    return worst(v, expect_word(ctx, "FLAGS", nth(r.actual.flags(), 0), OF | SF | AF, ref(), kArithmetic));
  }
};
REGISTER_TEST(D1AddFlags);

}  // namespace
}  // namespace i8086::testprog
