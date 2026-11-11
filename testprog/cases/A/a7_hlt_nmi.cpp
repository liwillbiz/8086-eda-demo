// A7 — HLT, then NMI (brief §7 A): the CPU signals the halt state on the bus and stops;
// a pulse on NMI brings it out through the type-2 handler, back to the instruction after HLT.

#include "interrupts.hpp"
#include "margin.hpp"

namespace i8086::testprog {
namespace {

class A7HltNmi final : public RomCase {
 public:
  std::string_view id() const override { return "A7"; }
  std::string_view title() const override { return "HLT then NMI"; }

 protected:
  std::string_view rom_name() const override { return "a7_hlt_nmi"; }
  RomRun execute(TestContext& ctx) override {
    RomRun first = run_rom(ctx, rom_name(), bounded_run());
    if (!first.actual.halted) return first;
    pulse_at_ = first.result.cycles + 4;
    tester::RunOpts o = bounded_run();
    o.stimuli.push_back({.pin = "NMI", .high = true, .clk = pulse_at_, .at_ns = {}});
    o.stimuli.push_back({.pin = "NMI", .high = false, .clk = pulse_at_ + 2, .at_ns = {}});
    const golden::InterruptRequest req{.instruction_index = golden::InterruptRequest::kDuringHalt, .nmi = true};
    return run_rom(ctx, rom_name(), o, {&req, 1});
  }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto& trace = ctx.t.last_trace();
    const auto halts = transactions_of(trace, model::BusCycleType::Halt);
    const bool halt_first = !halts.empty() && halts.front().start_clk < pulse_at_;
    Verdict v = check(ctx, "halt_status_before_nmi", halt_first ? 1 : 0, exact(1, ref() + ", brief §4.1"));
    v = worst(v, check(ctx, "type2_entries", static_cast<double>(vector_fetches(trace, 2)), exact(1, ref())));
    const auto d = r.actual.data();
    v = worst(v, expect_word(ctx, "report_before_hlt", nth(d, 0), 1, ref()));
    v = worst(v, expect_word(ctx, "report_in_handler", nth(d, 1), 2, ref()));
    v = worst(v, expect_word(ctx, "report_after_iret", nth(d, 3), 3, ref()));
    return worst(v, expect_word(ctx, "stacked_IP", nth(d, 2), nth(d, 4).value_or(0), "brief §5 (after HLT)"));
  }

 private:
  uint64_t pulse_at_ = 0;
};
REGISTER_TEST(A7HltNmi);

}  // namespace
}  // namespace i8086::testprog
