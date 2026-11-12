// D7 — INTR during REP MOVSB (brief §7 D): the tester raises INTR after the 20th byte is
// copied. The 8086 takes it between iterations and stacks the IP of the REP prefix, so the
// copy resumes after IRET and completes with CX = 0. The handler reports CX plus the bytes
// already copied, which must be the full count.
//
// The golden emulator runs REP as one instruction, so its request is taken just before the
// REP; its I/O and non-stack memory writes are the same as the DUT's (design doc §M8).

#include "interrupts.hpp"
#include "margin.hpp"

namespace i8086::testprog {
namespace {

constexpr uint8_t kType = 0x41;
constexpr uint32_t kDest = 0x3000, kCount = 64;
constexpr int kWritesBeforeIntr = 20;

bool to_dest(const tester::BusTransaction& tx) {
  return tx.type == model::BusCycleType::MemWrite && tx.address >= kDest && tx.address < kDest + kCount;
}

class D7RepIntr final : public RomCase {
 public:
  std::string_view id() const override { return "D7"; }
  std::string_view title() const override { return "INTR during REP MOVSB"; }

 protected:
  std::string_view rom_name() const override { return "d7_rep_intr"; }
  RomRun execute(TestContext& ctx) override {
    RomRun first = run_rom(ctx, rom_name(), bounded_run());
    const auto d = first.expected.data();
    if (!first.actual.halted || d.empty()) return first;
    std::optional<uint64_t> at;
    int seen = 0;
    for (const auto& tx : ctx.t.last_trace().transactions) {
      if (to_dest(tx) && ++seen == kWritesBeforeIntr) at = tx.start_clk;
    }
    const auto index = golden_index_at(ctx, rom_name(), d.back());
    if (!at || !index) return first;
    tester::RunOpts o = bounded_run();
    o.stimuli.push_back({.pin = "INTR", .high = true, .clk = *at, .at_ns = {}});
    o.inta_type = kType;
    const golden::InterruptRequest req{.instruction_index = *index, .nmi = false, .type = kType};
    return run_rom(ctx, rom_name(), o, {&req, 1});
  }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto& trace = ctx.t.last_trace();
    const auto inta = transactions_of(trace, model::BusCycleType::InterruptAck);
    int before = 0;
    if (!inta.empty()) {
      for (const auto& tx : trace.transactions) before += to_dest(tx) && tx.start_clk < inta.front().start_clk;
    }
    spec::Limit mid = info(ref() + " (interrupted mid-REP)");
    mid.min = 1;
    mid.max = kCount - 1;
    Verdict v = check(ctx, "inta_cycles", static_cast<double>(inta.size()), exact(2, "brief §4.3"));
    v = worst(v, check(ctx, "bytes_copied_before_intr", before, mid));
    const auto d = r.actual.data();
    v = worst(v, expect_word(ctx, "stacked_IP", nth(d, 0), nth(d, 8).value_or(0), ref() + " (REP prefix)"));
    v = worst(v, expect_word(ctx, "CX_plus_copied_in_handler", nth(d, 1), kCount, ref()));
    v = worst(v, expect_word(ctx, "CX", nth(d, 2), 0, ref()));
    v = worst(v, expect_word(ctx, "SI", nth(d, 3), 0x2000 + kCount, ref()));
    v = worst(v, expect_word(ctx, "DI", nth(d, 4), kDest + kCount, ref()));
    const auto byte = [](unsigned i) { return static_cast<uint16_t>((1 + 3 * i) & 0xFF); };
    const auto word = [&](unsigned i) { return static_cast<uint16_t>(byte(i) | byte(i + 1) << 8); };
    v = worst(v, expect_word(ctx, "dest_3000h", nth(d, 5), word(0), ref()));
    v = worst(v, expect_word(ctx, "dest_301Eh", nth(d, 6), word(30), ref()));
    return worst(v, expect_word(ctx, "dest_303Eh", nth(d, 7), word(62), ref()));
  }
};
REGISTER_TEST(D7RepIntr);

}  // namespace
}  // namespace i8086::testprog
