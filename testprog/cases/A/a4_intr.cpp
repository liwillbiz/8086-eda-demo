// A4 — INTR (brief §7 A, §4.3): with IF=1 the tester raises INTR and, like an 8259, answers
// the second of two back-to-back INTA cycles with type 40h. The CPU fetches its vector from
// 00100h, stacks CS:IP of the next instruction after the STI shadow, and clears IF and TF.

#include "interrupts.hpp"
#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

constexpr uint16_t kRomSegment = 0xFF00;  // testprog/roms/inc/i86rom.inc ROM_SEG
constexpr uint8_t kType = 0x40;

class A4Intr final : public RomCase {
 public:
  std::string_view id() const override { return "A4"; }
  std::string_view title() const override { return "INTR and INTA cycles"; }

 protected:
  std::string_view rom_name() const override { return "a4_intr"; }
  RomRun execute(TestContext& ctx) override {
    tester::RunOpts o;
    o.max_cycles = 20'000;
    o.stimuli.push_back({.pin = "INTR", .high = true, .clk = 0, .at_ns = {}});
    o.inta_type = kType;
    const golden::InterruptRequest req{.instruction_index = 0, .nmi = false, .type = kType};
    return run_rom(ctx, rom_name(), o, {&req, 1});
  }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    using T = model::BusCycleType;
    const auto& txs = ctx.t.last_trace().transactions;
    std::vector<std::size_t> inta;
    for (std::size_t i = 0; i < txs.size(); ++i) {
      if (txs[i].type == T::InterruptAck) inta.push_back(i);
    }
    Verdict v = check(ctx, "inta_cycles", static_cast<double>(inta.size()), exact(2, ref()));
    const bool pair = inta.size() == 2 && inta[1] == inta[0] + 1;
    v = worst(v, check(ctx, "inta_back_to_back", pair ? 1 : 0, exact(1, "brief §4.3")));
    std::optional<uint16_t> type, vector_at;
    if (inta.size() == 2) {
      type = static_cast<uint16_t>(txs[inta[1]].data & 0xFF);
      for (std::size_t i = inta[1] + 1; i < txs.size() && !vector_at; ++i) {
        if (txs[i].type == T::MemRead) vector_at = static_cast<uint16_t>(txs[i].address);
      }
    }
    v = worst(v, expect_word(ctx, "inta_type", type, kType, "brief §4.3"));
    v = worst(v, expect_word(ctx, "vector_address", vector_at, kType * 4, ref() + " (00100h)"));

    const auto d = r.actual.data();
    v = worst(v, expect_word(ctx, "stacked_IP", nth(d, 0), nth(d, 4).value_or(0), ref() + " (after the STI shadow)"));
    v = worst(v, expect_word(ctx, "stacked_CS", nth(d, 1), kRomSegment, ref()));
    v = worst(v, expect_word(ctx, "stacked_IF", nth(d, 2), flag::IF, "brief §5", flag::IF));
    v = worst(v, expect_word(ctx, "sti_shadow_ran", nth(d, 3), 0x1111, "brief §5"));
    return worst(v, expect_word(ctx, "IF_TF_in_handler", nth(r.actual.flags(), 0), 0, "brief §5",
                                flag::IF | flag::TF));
  }
};
REGISTER_TEST(A4Intr);

}  // namespace
}  // namespace i8086::testprog
