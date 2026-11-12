// D9 — FLAGS identity (brief §7 D, §1.4): XOR AX,AX; PUSH AX; POPF; PUSHF; POP AX reads
// bits 15-12 as 1 on a genuine 8086.

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class D9FlagsIdentity final : public RomCase {
 public:
  std::string_view id() const override { return "D9"; }
  std::string_view title() const override { return "FLAGS identity"; }

 protected:
  std::string_view rom_name() const override { return "d9_flags_identity"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto d = r.actual.data();
    const Verdict v = expect_word(ctx, "bits15_12_after_POPF_0", nth(d, 0), 0xF000, ref(), 0xF000);
    return worst(v, expect_word(ctx, "bits15_12_after_POPF_FEFF", nth(d, 1), 0xF000, ref(), 0xF000));
  }
};
REGISTER_TEST(D9FlagsIdentity);

}  // namespace
}  // namespace i8086::testprog
