// B5 — Strobe timing (brief §7 B): with every output loaded by CL = 100 pF, the strobe
// widths TLHLL, TRLRH, TWLWH and the delays TCLAV, TRHAV are within limits. Each value is
// the worst case over every bus cycle of the checksum ROM.

#include <algorithm>
#include <format>
#include <optional>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

using model::Logic;

constexpr double kLoadPf = 100;  // spec.yaml conditions AC_CL max (brief §4.2)

// Widths of every pulse of `pin` at `level` (ns), from the resolved pin changes.
std::vector<double> pulses(const tester::BusTrace& trace, int pin, Logic level) {
  std::vector<double> out;
  std::optional<int64_t> start;
  for (const auto& c : trace.cycles) {
    for (const auto& e : c.events) {
      if (e.pin != pin) continue;
      if (e.value == level) {
        start = e.delay_ps;
      } else if (start) {
        out.push_back(static_cast<double>(e.delay_ps - *start) / 1000);
        start.reset();
      }
    }
  }
  return out;
}

class B5StrobeTiming final : public TestCase {
 public:
  std::string_view id() const override { return "B5"; }
  std::string_view title() const override { return "Strobe timing"; }
  std::vector<std::string_view> spec_symbols() const override {
    return {"TLHLL", "TRLRH", "TWLWH", "TCLAV", "TRHAV"};
  }

  Verdict run(TestContext& ctx) override {
    tester::LevelSet levels;
    levels.vcc = ctx.vcc_v;
    levels.cl_pf = kLoadPf;
    ctx.t.set_levels(levels);
    setup_functional(ctx);
    const double period = ctx.spec.grade(ctx.grade).tclcl_min_ns;
    const std::string note = std::format("CL = {:g} pF", kLoadPf);

    const Outcome o = rom_outcome(ctx, "b1_checksum");
    Verdict v = check(ctx, "program_pass", o.pass ? 1 : 0, exact(1, "brief §7 B5"), o.why);
    const tester::BusTrace& trace = ctx.t.last_trace();
    const auto pin = [&](std::string_view name) { return ctx.spec.pin(name).num; };

    // Judges the smallest value against the limit's min and the largest against its max.
    const auto judge = [&](const std::string& sym, const std::vector<double>& xs) {
      const spec::Limit lim = ctx.spec.ac(sym, ctx.grade, period);
      if (xs.empty()) return missing(ctx, sym, lim, "no such edge in the trace");
      Verdict r = Verdict::Pass;
      const std::string n = std::format("worst of {}; {}", xs.size(), note);
      if (lim.min) {
        spec::Limit lo = lim;
        lo.max.reset();
        r = worst(r, check(ctx, lim.max ? sym + ".min" : sym, std::ranges::min(xs), lo, n));
      }
      if (lim.max) {
        spec::Limit hi = lim;
        hi.min.reset();
        r = worst(r, check(ctx, lim.min ? sym + ".max" : sym, std::ranges::max(xs), hi, n));
      }
      return r;
    };

    v = worst(v, judge("TLHLL", pulses(trace, pin("ALE"), Logic::L1)));
    v = worst(v, judge("TRLRH", pulses(trace, pin("RD_N"), Logic::L0)));
    v = worst(v, judge("TWLWH", pulses(trace, pin("WR_N"), Logic::L0)));

    std::vector<int> address;
    for (const auto& p : ctx.spec.pins()) {
      if (p.bus == "AD" || p.bus == "A") address.push_back(p.num);
    }
    const auto is_address = [&](int n) { return std::ranges::find(address, n) != address.end(); };
    const int clk = pin("CLK"), rd = pin("RD_N");

    // TCLAV: CLK falling edge that starts T1 to each address pin change in T1.
    // TRHAV: RD# rising to the first address pin of the next bus cycle leaving its old value.
    std::vector<double> tclav, trhav;
    std::optional<int64_t> rd_rise;
    for (const auto& tx : trace.transactions) {
      if (tx.type == model::BusCycleType::None || tx.start_clk >= trace.cycles.size()) continue;
      const auto& c = trace.cycles[tx.start_clk];
      std::optional<int64_t> t1, first, last;
      for (const auto& e : c.events) {
        if (e.pin == clk && e.value == Logic::L0) t1 = e.delay_ps;
        if (!is_address(e.pin) || e.value == Logic::Z) continue;
        if (!first) first = e.delay_ps;
        last = e.delay_ps;
      }
      if (t1 && first) tclav.push_back(static_cast<double>(*first - *t1) / 1000);
      if (t1 && last) tclav.push_back(static_cast<double>(*last - *t1) / 1000);
      if (rd_rise && first) trhav.push_back(static_cast<double>(*first - *rd_rise) / 1000);
      rd_rise.reset();
      for (std::size_t k = tx.start_clk; k < trace.cycles.size(); ++k) {
        const auto& events = trace.cycles[k].events;
        const auto it = std::ranges::find_if(events, [&](const model::PinEvent& e) {
          return e.pin == rd && e.value == Logic::L1;
        });
        if (it != events.end()) {
          rd_rise = it->delay_ps;
          break;
        }
        if (k > tx.start_clk + 4 + static_cast<std::size_t>(tx.wait_states)) break;
      }
    }
    v = worst(v, judge("TCLAV", tclav));
    return worst(v, judge("TRHAV", trhav));
  }
};
REGISTER_TEST(B5StrobeTiming);

}  // namespace
}  // namespace i8086::testprog
