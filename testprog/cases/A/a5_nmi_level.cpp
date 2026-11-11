// A5 — NMI is edge triggered (brief §7 A): the tester raises NMI after the CPU halts and
// holds it high. The type-2 handler must run exactly once for the one rising edge.

#include "interrupts.hpp"
#include "margin.hpp"

namespace i8086::testprog {
namespace {

class A5NmiLevel final : public RomCase {
 public:
  std::string_view id() const override { return "A5"; }
  std::string_view title() const override { return "NMI held high"; }

 protected:
  std::string_view rom_name() const override { return "a5_nmi_level"; }
  RomRun execute(TestContext& ctx) override {
    RomRun first = run_rom(ctx, rom_name(), bounded_run());
    if (!first.actual.halted) return first;
    tester::RunOpts o = bounded_run();
    o.stimuli.push_back({.pin = "NMI", .high = true, .clk = first.result.cycles + 4, .at_ns = {}});
    const golden::InterruptRequest req{.instruction_index = golden::InterruptRequest::kDuringHalt, .nmi = true};
    return run_rom(ctx, rom_name(), o, {&req, 1});
  }
  Verdict brief_checks(TestContext& ctx, const RomRun& r) override {
    const auto d = r.actual.data();
    Verdict v = check(ctx, "type2_entries", static_cast<double>(vector_fetches(ctx.t.last_trace(), 2)),
                      exact(1, ref() + " (one per rising edge)"));
    v = worst(v, expect_word(ctx, "entries_after_edge", nth(d, 1), 1, ref()));
    v = worst(v, expect_word(ctx, "entries_level_held", nth(d, 2), 1, ref()));
    return worst(v, expect_word(ctx, "stacked_IP", nth(d, 0), nth(d, 3).value_or(0), "brief §5 (after HLT)"));
  }
};
REGISTER_TEST(A5NmiLevel);

}  // namespace
}  // namespace i8086::testprog
