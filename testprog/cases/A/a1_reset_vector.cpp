// A1 — Reset vector (brief §7 A, §5): after RESET the first bus cycle is a code fetch at
// FFFF0h, about 7 CLK after RESET falls. Checked in vector mode (reset_vector.pat) and in
// memory-emulation mode (boot ROM).

#include <format>

#include "functional.hpp"
#include "protocol.hpp"
#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

constexpr uint32_t kResetVector = 0xFFFF0;  // brief §5
constexpr double kResetLatencyClk = 7;      // brief §5: "about 7 clocks"

class A1ResetVector final : public TestCase {
 public:
  std::string_view id() const override { return "A1"; }
  std::string_view title() const override { return "Reset vector"; }
  std::vector<std::string_view> spec_symbols() const override { return {"TCLAV"}; }

  Verdict run(TestContext& ctx) override {
    setup_functional(ctx);
    const std::string ref = "brief §5";

    tester::Pattern pat = pattern("reset_vector");
    for (auto& t : pat.timings) t.attrs["grade"] = std::string(spec::to_string(ctx.grade));
    const tester::FunctionalResult vec = ctx.t.run_pattern(pat);
    std::string note;
    if (!vec.fails.empty()) {
      const auto& f = vec.fails.front();
      note = std::format("{} fail(s); first: vector {} pin {} expected {}", vec.fails.size(), f.vector,
                         f.pin, f.expected);
    }
    Verdict v = check(ctx, "pattern_fails", static_cast<double>(vec.fails.size()),
                      exact(0, ref + ", reset_vector.pat"), note);

    tester::MemoryImage image;
    const auto boot = rom("boot");
    image.load(kRomTop - static_cast<uint32_t>(boot.size()), boot);
    tester::RunOpts opts;
    opts.max_cycles = 2000;
    ctx.t.run_program(image, opts);
    const auto& trace = ctx.t.last_trace();
    if (trace.transactions.empty()) {
      ctx.log.record("first_fetch_address", std::nullopt, exact(kResetVector, ref), Verdict::Fail,
                     "no bus cycle after RESET");
      return Verdict::Fail;
    }
    const auto& first = trace.transactions.front();
    v = worst(v, check(ctx, "first_cycle_is_code_fetch",
                       first.type == model::BusCycleType::CodeFetch ? 1 : 0, exact(1, ref)));
    v = worst(v, check(ctx, "first_fetch_address", first.address, exact(kResetVector, ref), hex(first.address, 5)));

    const int reset = ctx.spec.pin("RESET").num;
    std::optional<uint64_t> reset_fall;
    for (const auto& c : trace.cycles) {
      for (const auto& e : c.events) {
        if (e.pin == reset && e.value == model::Logic::L0 && !reset_fall) reset_fall = c.clk;
      }
    }
    spec::Limit latency{kResetLatencyClk - 1, kResetLatencyClk + 1, "CLK", ref + " (about 7 CLK)", false};
    if (!reset_fall) {
      ctx.log.record("reset_to_first_fetch", std::nullopt, latency, Verdict::Fail, "RESET never fell");
      return Verdict::Fail;
    }
    v = worst(v, check(ctx, "reset_to_first_fetch", static_cast<double>(first.start_clk - *reset_fall), latency));

    const auto protocol = check_protocol(trace, ctx.spec);
    return worst(v, check(ctx, "protocol_violations", static_cast<double>(protocol.size()),
                          exact(0, "brief §4.1, spec decode"), protocol.empty() ? "" : protocol.front()));
  }
};
REGISTER_TEST(A1ResetVector);

}  // namespace
}  // namespace i8086::testprog
