// D5 — Segment math wrap (brief §7 D, §1.5): MOV ES,FFFFh; MOV BYTE ES:[0010h],AAh writes
// at 00000h, because the 8086 has 20 address lines (no A20).

#include <format>

#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

class D5SegmentWrap final : public RomCase {
 public:
  std::string_view id() const override { return "D5"; }
  std::string_view title() const override { return "Segment math wrap"; }

 protected:
  std::string_view rom_name() const override { return "d5_segment_wrap"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    std::optional<uint32_t> addr;
    for (const auto& tx : ctx.t.last_trace().transactions) {
      if (tx.type == model::BusCycleType::MemWrite && tx.bhe_n && (tx.data & 0xFF) == 0xAA) addr = tx.address;
    }
    Verdict v = Verdict::Fail;
    if (addr) {
      v = check(ctx, "AAh_write_address", *addr, exact(0x00000, ref()), hex(*addr, 5));
    } else {
      ctx.log.record("AAh_write_address", std::nullopt, exact(0x00000, ref()), Verdict::Fail,
                     "no byte write of AAh on the bus");
    }
    return worst(v, expect_word(ctx, "byte_at_00000h", nth(r.actual.data(), 0), 0x00AA, ref()));
  }
};
REGISTER_TEST(D5SegmentWrap);

}  // namespace
}  // namespace i8086::testprog
