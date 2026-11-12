// A6 — HOLD/HLDA (brief §7 A, §4.4): the tester raises HOLD half way through a memory-heavy
// ROM and drops it 24 CLKs later. HLDA must rise only after the current bus cycle ends;
// while it is high the CPU runs no bus cycle and floats AD, A, BHE#, RD#, WR#, M/IO#,
// DT/R# and DEN#; afterwards the ROM finishes with the golden results.

#include <format>

#include "interrupts.hpp"
#include "margin.hpp"

namespace i8086::testprog {
namespace {

constexpr uint64_t kHoldClocks = 24;

class A6Hold final : public RomCase {
 public:
  std::string_view id() const override { return "A6"; }
  std::string_view title() const override { return "HOLD and HLDA"; }

 protected:
  std::string_view rom_name() const override { return "a6_hold"; }
  RomRun execute(TestContext& ctx) override {
    RomRun first = run_rom(ctx, rom_name(), bounded_run());
    if (!first.actual.halted) return first;
    hold_at_ = first.result.cycles / 2;
    tester::RunOpts o = bounded_run();
    o.stimuli.push_back({.pin = "HOLD", .high = true, .clk = hold_at_, .at_ns = {}});
    o.stimuli.push_back({.pin = "HOLD", .high = false, .clk = hold_at_ + kHoldClocks, .at_ns = {}});
    return run_rom(ctx, rom_name(), o);
  }
  Verdict brief_checks(TestContext& ctx, const RomRun&) override {
    const auto& trace = ctx.t.last_trace();
    const int hlda = ctx.spec.pin("HLDA").num;
    const auto rise = first_change(trace, hlda, model::Logic::L1, hold_at_);
    if (!rise) return missing(ctx, "hlda_latency_clk", info(ref(), "CLK"), "HLDA never rose");
    const auto fall = first_change(trace, hlda, model::Logic::L0, *rise);
    Verdict v = check(ctx, "hlda_latency_clk", static_cast<double>(*rise - hold_at_), info(ref(), "CLK"));
    v = worst(v, check(ctx, "hlda_released", fall ? 1 : 0, exact(1, ref())));

    int cut = 0, during = 0;
    for (const auto& tx : trace.transactions) {
      if (tx.start_clk < *rise && tx.start_clk + 3 + static_cast<uint64_t>(tx.wait_states) > *rise) ++cut;
      if (tx.start_clk >= *rise && (!fall || tx.start_clk <= *fall)) ++during;
    }
    v = worst(v, check(ctx, "cycles_cut_by_hold", cut, exact(0, "brief §4.4 (after the current cycle)")));
    v = worst(v, check(ctx, "bus_cycles_during_hold", during, exact(0, "brief §4.4")));

    std::string driven;
    int n = 0;
    for (const auto& p : ctx.spec.pins()) {
      const bool bus = p.name.starts_with("AD") || p.name.starts_with("A1") ||
                       p.name == "BHE_N" || p.name == "RD_N" || p.name == "WR_N" ||
                       p.name == "M_IO_N" || p.name == "DT_R_N" || p.name == "DEN_N";
      if (!bus || level_at(trace, p.num, *rise + 2) == model::Logic::Z) continue;
      ++n;
      driven += (driven.empty() ? "" : " ") + p.name;
    }
    return worst(v, check(ctx, "pins_not_floating", n, exact(0, "brief §4.4"),
                          driven.empty() ? "" : std::format("driven while HLDA high: {}", driven)));
  }

 private:
  uint64_t hold_at_ = 0;
};
REGISTER_TEST(A6Hold);

}  // namespace
}  // namespace i8086::testprog
