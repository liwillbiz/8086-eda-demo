// A2 — Reset register state (brief §7 A, §5): the boot ROM reports DS, SS, ES and FLAGS
// before touching them. All zero, IF = 0; bits 15-12 of a pushed FLAGS read as 1.

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class A2ResetRegisters final : public RomCase {
 public:
  std::string_view id() const override { return "A2"; }
  std::string_view title() const override { return "Reset register state"; }

 protected:
  std::string_view rom_name() const override { return "boot"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto data = r.actual.data();
    const auto flags = r.actual.flags();
    const std::string ref = "brief §5";
    Verdict v = expect_word(ctx, "DS", nth(data, 0), 0x0000, ref);
    v = worst(v, expect_word(ctx, "ES", nth(data, 1), 0x0000, ref));
    v = worst(v, expect_word(ctx, "SS", nth(data, 2), 0x0000, ref));
    v = worst(v, expect_word(ctx, "FLAGS", nth(flags, 0), 0x0000, ref, 0x0FD5));
    return worst(v, expect_word(ctx, "IF", nth(flags, 0), 0x0000, ref, flag::IF));
  }
};
REGISTER_TEST(A2ResetRegisters);

}  // namespace
}  // namespace i8086::testprog
