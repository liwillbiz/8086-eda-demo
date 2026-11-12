// D4 — Addressing and segment defaults (brief §7 D, §1.5): MOV AX,[BP+2] reads through SS
// (S4 S3 = 01), MOV AX,[BX+2] through DS (11), and an ES: override through ES (00).
// The status bits are read from the A17/S4 and A16/S3 pins during each read cycle.

#include <algorithm>
#include <format>

#include "protocol.hpp"
#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

struct Read {
  std::string_view param;
  uint32_t address;
  std::string_view segment;  // spec decode.segment identifier
};

class D4SegmentDefaults final : public RomCase {
 public:
  std::string_view id() const override { return "D4"; }
  std::string_view title() const override { return "Addressing / segment defaults"; }

 protected:
  std::string_view rom_name() const override { return "d4_segment_defaults"; }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    static constexpr Read kReads[] = {
        {"S4S3_BP_plus_2", 0x03012, "SS"}, {"S4S3_BX_plus_2", 0x02012, "DS"}, {"S4S3_ES_override", 0x04012, "ES"}};
    const auto& trace = ctx.t.last_trace();
    const auto& table = ctx.spec.decode().segment;
    const std::string ref = "brief §1.5, §3.3, spec decode.segment";
    Verdict v = expect_word(ctx, "word_via_SS", nth(r.actual.data(), 0), 0xA55A, ref);
    v = worst(v, expect_word(ctx, "word_via_DS", nth(r.actual.data(), 1), 0xB66B, ref));
    v = worst(v, expect_word(ctx, "word_via_ES", nth(r.actual.data(), 2), 0xC77C, ref));
    for (const auto& want : kReads) {
      const auto want_code = static_cast<double>(std::find(table.begin(), table.end(), want.segment) - table.begin());
      const tester::BusTransaction* tx = nullptr;
      for (const auto& t : trace.transactions) {
        if (t.type == model::BusCycleType::MemRead && t.address == want.address) tx = &t;
      }
      if (!tx) {
        ctx.log.record(std::string(want.param), std::nullopt, exact(want_code, ref), Verdict::Fail,
                       std::format("no read cycle at {}", hex(want.address, 5)));
        v = worst(v, Verdict::Fail);
        continue;
      }
      const CycleStatus s = cycle_status(trace, ctx.spec, *tx);
      const double code = s.s3 < 0 || s.s4 < 0 ? -1 : s.s4 * 2 + s.s3;
      v = worst(v, check(ctx, std::string(want.param), code, exact(want_code, ref),
                         std::format("read {}: expected {}, got {}", hex(want.address, 5), want.segment,
                                     s.segment.empty() ? "X" : s.segment)));
    }
    return v;
  }
};
REGISTER_TEST(D4SegmentDefaults);

}  // namespace
}  // namespace i8086::testprog
