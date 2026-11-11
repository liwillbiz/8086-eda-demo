// B6 — INTR/NMI setup, TINVCH (brief §7 B): with the CPU halted, the tester raises NMI or
// INTR closer and closer to a CLK rising edge. An input inside its setup window is not
// recognized until the next edge, so the wake-up latency grows by a CLK; a search per pin
// finds the setup the die needs and judges it against TINVCH min.

#include "interrupts.hpp"
#include "margin.hpp"

namespace i8086::testprog {
namespace {

constexpr uint8_t kIntrType = 0x42;

class B6IntSetup final : public TestCase {
 public:
  std::string_view id() const override { return "B6"; }
  std::string_view title() const override { return "INTR/NMI setup (TINVCH)"; }
  std::vector<std::string_view> spec_symbols() const override { return {"TINVCH"}; }

  Verdict run(TestContext& ctx) override {
    const double period = ctx.spec.grade(ctx.grade).tclcl_min_ns;
    const double high = spec_clk_high(ctx.spec, ctx.grade, period);
    const double rise = period - high;  // CLK rises this long after the period starts
    const spec::Limit tinvch = ctx.spec.ac("TINVCH", ctx.grade, period);
    tester::TimingSet ts = functional_timing(ctx.spec, ctx.grade);
    ts.clk_high_ns = high;
    ctx.t.set_timing(0, ts);

    const RomRun first = run_rom(ctx, "b6_int_setup", bounded_run());
    Verdict v = Verdict::Pass;
    for (const bool nmi : {true, false}) {
      const std::string name = nmi ? "nmi" : "intr";
      if (!first.actual.halted) {
        v = worst(v, missing(ctx, name + "_setup_required", at_most_min(tinvch), "ROM did not halt"));
        continue;
      }
      const uint64_t at = first.result.cycles + 4;
      // CLKs from the stimulus to the first bus cycle of the interrupt entry.
      const auto latency = [&](double setup) -> std::optional<uint64_t> {
        tester::RunOpts o = bounded_run();
        o.inta_type = kIntrType;
        o.stimuli.push_back({.pin = nmi ? "NMI" : "INTR", .high = true, .clk = at, .at_ns = rise - setup});
        const golden::InterruptRequest req{
            .instruction_index = golden::InterruptRequest::kDuringHalt, .nmi = nmi, .type = kIntrType};
        try {
          const RomRun r = run_rom(ctx, "b6_int_setup", o, {&req, 1});
          if (!r.actual.halted || !r.protocol.empty() || !diff(r.expected, r.actual, {}).empty()) return std::nullopt;
        } catch (const std::exception&) {
          return std::nullopt;
        }
        const auto entry = nmi ? model::BusCycleType::MemWrite : model::BusCycleType::InterruptAck;
        for (const auto& tx : ctx.t.last_trace().transactions) {
          if (tx.type == entry && tx.start_clk >= at) return tx.start_clk - at;
        }
        return std::nullopt;
      };
      const auto reference = latency(rise);  // changes with the CLK falling edge
      if (!reference) {
        v = worst(v, missing(ctx, name + "_setup_required", at_most_min(tinvch), "not recognized with a full CLK low phase of setup"));
        continue;
      }
      const auto recognized = [&](double setup) { return latency(setup) == reference; };
      v = worst(v, check(ctx, name + "_pass_at_spec_setup", recognized(*tinvch.min) ? 1 : 0,
                         exact(1, "brief §7 B6, setup = TINVCH min")));
      const auto edge = find_edge(recognized, rise, -(high - 0.25), 0.25);
      v = worst(v, edge ? check(ctx, name + "_setup_required", *edge, at_most_min(tinvch), "search resolution 0.25 ns")
                        : missing(ctx, name + "_setup_required", at_most_min(tinvch), "search failed"));
    }
    return v;
  }
};
REGISTER_TEST(B6IntSetup);

}  // namespace
}  // namespace i8086::testprog
