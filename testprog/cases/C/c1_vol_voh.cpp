// C1 — VOL / VOH (brief §7 C): force IOL = 2.5 mA / IOH = -400 µA on each output and
// measure the voltage. The boot ROM is stopped after every CLK of its first bus cycles, so
// each output is caught both low and high where the program drives it that way. AD pins
// are measured only while the device drives them (T1, and write cycles).

#include <format>
#include <limits>
#include <set>

#include "margin.hpp"

namespace i8086::testprog {
namespace {

using model::Logic;

constexpr double kIolMa = 2.5;   // spec VOL condition IOL_mA
constexpr double kIohMa = -0.4;  // spec VOH condition IOH_uA = -400

Logic last_state(const tester::BusTrace& trace, int pin) {
  for (auto c = trace.cycles.rbegin(); c != trace.cycles.rend(); ++c) {
    for (auto e = c->events.rbegin(); e != c->events.rend(); ++e) {
      if (e->pin == pin) return e->value;
    }
  }
  return Logic::Z;
}

bool device_drives_ad(const model::BusState& b) {
  using model::BusCycleType;
  return b.t == model::TState::T1 || b.cycle == BusCycleType::MemWrite || b.cycle == BusCycleType::IoWrite;
}

std::string names(const std::set<std::string>& s) {
  std::string out;
  for (const auto& n : s) out += (out.empty() ? "" : " ") + n;
  return out;
}

class C1VolVoh final : public TestCase {
 public:
  std::string_view id() const override { return "C1"; }
  std::string_view title() const override { return "VOL / VOH"; }
  std::vector<std::string_view> spec_symbols() const override { return {"VOL", "VOH"}; }

  Verdict run(TestContext& ctx) override {
    setup_functional(ctx);
    const auto boot = rom("boot");
    tester::MemoryImage image;
    image.load(kRomTop - static_cast<uint32_t>(boot.size()), boot);

    std::vector<const spec::Pin*> outputs;
    for (const auto& p : ctx.spec.pins()) {
      if (p.dir == spec::Dir::Out || p.dir == spec::Dir::Io) outputs.push_back(&p);
    }
    double vol = -std::numeric_limits<double>::infinity(), voh = std::numeric_limits<double>::infinity();
    std::string vol_pin, voh_pin;
    std::set<std::string> low, high;
    for (uint64_t n = 6; n <= 48; ++n) {
      tester::RunOpts opts;
      opts.max_cycles = n;
      ctx.t.run_program(image, opts);
      const auto& trace = ctx.t.last_trace();
      if (trace.cycles.empty()) continue;
      const bool ad_ok = device_drives_ad(trace.cycles.back().bus);
      for (const spec::Pin* p : outputs) {
        if (p->bus == "AD" && !ad_ok) continue;
        const Logic s = last_state(trace, p->num);
        if (s == Logic::L0) {
          const double v = ctx.t.force_i_measure_v(p->name, kIolMa);
          low.insert(p->name);
          if (v > vol) {
            vol = v;
            vol_pin = p->name;
          }
        } else if (s == Logic::L1) {
          const double v = ctx.t.force_i_measure_v(p->name, kIohMa);
          high.insert(p->name);
          if (v < voh) {
            voh = v;
            voh_pin = p->name;
          }
        }
      }
    }

    std::set<std::string> never_low, never_high;
    for (const spec::Pin* p : outputs) {
      if (!low.contains(p->name)) never_low.insert(p->name);
      if (!high.contains(p->name)) never_high.insert(p->name);
    }
    const spec::Limit vol_lim = ctx.spec.dc("VOL", ctx.grade);
    const spec::Limit voh_lim = ctx.spec.dc("VOH", ctx.grade);
    Verdict v = low.empty() ? missing(ctx, "VOL", vol_lim, "no output was ever low")
                            : check(ctx, "VOL", vol, vol_lim,
                                    std::format("worst pin {} of {}, IOL {:g} mA", vol_pin, low.size(), kIolMa));
    v = worst(v, high.empty() ? missing(ctx, "VOH", voh_lim, "no output was ever high")
                              : check(ctx, "VOH", voh, voh_lim,
                                      std::format("worst pin {} of {}, IOH {:g} uA", voh_pin, high.size(), kIohMa * 1000)));
    ctx.log.record("outputs_not_seen_low", static_cast<double>(never_low.size()), info("brief §7 C1"),
                   Verdict::Pass, names(never_low));
    ctx.log.record("outputs_not_seen_high", static_cast<double>(never_high.size()), info("brief §7 C1"),
                   Verdict::Pass, names(never_high));
    return v;
  }
};
REGISTER_TEST(C1VolVoh);

}  // namespace
}  // namespace i8086::testprog
