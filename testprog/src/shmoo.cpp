#include "shmoo.hpp"

#include <algorithm>
#include <cmath>

#include "i8086/golden/emulator.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/model/die_profile.hpp"
#include "i8086/tester/electrical_model.hpp"
#include "i8086/tester/sim_tester.hpp"
#include "margin.hpp"

namespace i8086::testprog {

bool ShmooResult::in_box(const ShmooCell& c, const ShmooBox& b) const {
  constexpr double eps = 1e-9;
  return c.mhz <= b.fmax_mhz + eps && c.vcc_v >= kVccOperatingMin - eps && c.vcc_v <= kVccOperatingMax + eps;
}

bool ShmooResult::in_any_box(const ShmooCell& c) const {
  return std::ranges::any_of(boxes, [&](const ShmooBox& b) { return in_box(c, b); });
}

int ShmooResult::box_fails() const {
  return static_cast<int>(std::ranges::count_if(cells, [&](const ShmooCell& c) { return !c.pass && in_any_box(c); }));
}

ShmooResult run_shmoo(const ShmooConfig& cfg) {
  const spec::Spec spec = spec::Spec::load(cfg.spec_path);
  const model::DieProfile die = model::DieProfile::load(cfg.die_path);
  const golden::Emulator golden;

  ShmooResult r;
  r.die = die.name;
  r.temp_c = cfg.temp_c;
  r.vcc_v = cfg.vcc_v;
  if (r.vcc_v.empty()) {
    const double vmax = spec.abs_max("V_PIN").max.value_or(6.0);
    for (int i = 0; i <= 8; ++i) {
      if (const double v = 4.0 + 0.25 * i; v <= vmax) r.vcc_v.push_back(v);
    }
  }
  r.mhz = cfg.mhz;
  if (r.mhz.empty()) {
    for (int f = 2; f <= 12; ++f) r.mhz.push_back(f);
    r.mhz.push_back(12.5);
  }
  std::ranges::sort(r.vcc_v);
  std::ranges::sort(r.mhz);
  for (spec::Grade g : spec::kAllGrades) r.boxes.push_back({g, spec.grade(g).fclk_max_mhz});

  for (double vcc : r.vcc_v) {
    for (double mhz : r.mhz) {
      ShmooCell cell{vcc, mhz, true, {}, {}};
      tester::SimTester t(spec, model::make_behavioral(spec, die, {}), tester::ElectricalModel(die));
      Datalog scratch("shmoo");
      TestContext ctx{t, spec, spec::Grade::G8086, vcc, cfg.temp_c, scratch, &golden};
      try {
        tester::LevelSet levels;
        levels.vcc = vcc;
        t.set_levels(levels);
        t.set_temperature(cfg.temp_c);
        t.power(true);
        tester::TimingSet ts;
        ts.period_ns = 1000.0 / mhz;
        t.set_timing(0, ts);
      } catch (const std::exception& e) {
        cell.pass = false;
        cell.failed_rom = "setup";
        cell.why = e.what();
      }
      for (const SuiteRom& rom : functional_suite()) {
        if (!cell.pass) break;
        const Outcome o = rom_outcome(ctx, rom.name, rom.undefined_flags);
        if (!o.pass) {
          cell.pass = false;
          cell.failed_rom = std::string(rom.name);
          cell.why = o.why;
        }
      }
      r.cells.push_back(std::move(cell));
    }
  }
  return r;
}

}  // namespace i8086::testprog
