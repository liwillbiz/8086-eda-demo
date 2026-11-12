// D2 — INC preserves CF (brief §7 D, §2): STC; INC AX leaves CF = 1. DEC with CF = 1 and
// INC with CF = 0 are checked the same way.

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class D2IncCf final : public RomCase {
 public:
  std::string_view id() const override { return "D2"; }
  std::string_view title() const override { return "INC preserves CF"; }

 protected:
  std::string_view rom_name() const override { return "d2_inc_cf"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto f = r.actual.flags();
    Verdict v = expect_word(ctx, "CF_after_STC_INC", nth(f, 0), flag::CF, ref(), flag::CF);
    v = worst(v, expect_word(ctx, "CF_after_STC_DEC", nth(f, 1), flag::CF, ref(), flag::CF));
    return worst(v, expect_word(ctx, "CF_after_CLC_INC", nth(f, 2), 0, ref(), flag::CF));
  }
};
REGISTER_TEST(D2IncCf);

}  // namespace
}  // namespace i8086::testprog
