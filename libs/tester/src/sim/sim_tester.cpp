#include "i8086/tester/sim_tester.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "engine.hpp"
#include "timing.hpp"

namespace i8086::tester {

using detail::BusDecoder;
using detail::Engine;
using detail::ResolvedTiming;
using model::Logic;

namespace {

// Device facts from the brief that spec.yaml does not carry as limits.
constexpr int kResetClocks = 4;              // brief §3.1: RESET held >= 4 CLK
constexpr uint32_t kResetVector = 0xFFFF0;   // brief §5: first fetch after RESET
constexpr uint32_t kIoAddressMask = 0xFFFF;  // 64 K I/O ports

std::string fmt_v(double v) {
  std::string s = std::to_string(v);
  s.erase(s.find_last_not_of('0') + 1);
  if (s.back() == '.') s.pop_back();
  return s;
}

void check_range(const spec::Limit& lim, const std::string& sym, const std::string& what, double v,
                 const std::string& unit) {
  if (!std::isfinite(v)) throw std::out_of_range(what + " is not a finite number");
  if ((lim.min && v < *lim.min) || (lim.max && v > *lim.max)) {
    throw std::out_of_range(what + " = " + fmt_v(v) + " " + unit + " is outside abs_max " + sym +
                            " [" + (lim.min ? fmt_v(*lim.min) : "-inf") + ", " +
                            (lim.max ? fmt_v(*lim.max) : "inf") + "] " + unit);
  }
}

// PMU measurements happen with the clock at timing set 0's period (or the default).
model::Conditions static_conditions(const LevelSet& l, double temp_c, const std::map<int, TimingSet>& ts) {
  const auto it = ts.find(0);
  return {l.vcc, temp_c, it == ts.end() ? TimingSet{}.period_ns : it->second.period_ns};
}

bool is_drive(char c) { return c == '0' || c == '1' || c == 'Z'; }

Logic last_state(const BusTrace& trace, int pin) {
  for (auto c = trace.cycles.rbegin(); c != trace.cycles.rend(); ++c) {
    for (auto e = c->events.rbegin(); e != c->events.rend(); ++e) {
      if (e->pin == pin) return e->value;
    }
  }
  return Logic::Z;
}

}  // namespace

SimTester::SimTester(const spec::Spec& spec, std::unique_ptr<model::DeviceModel> dut,
                     ElectricalModel electrical)
    : spec_(spec), dut_(std::move(dut)), electrical_(std::move(electrical)) {
  if (!dut_) throw std::invalid_argument("SimTester: no DeviceModel");
}

void SimTester::set_levels(const LevelSet& levels) {
  const spec::Limit pin = spec_.abs_max("V_PIN");
  check_range(pin, "V_PIN", "VCC", levels.vcc, "V");
  check_range(pin, "V_PIN", "VIH", levels.vih, "V");
  check_range(pin, "V_PIN", "VIL", levels.vil, "V");
  check_range(pin, "V_PIN", "VCH", levels.vch, "V");
  check_range(pin, "V_PIN", "VCL", levels.vcl, "V");
  if (!std::isfinite(levels.cl_pf) || levels.cl_pf < 0) {
    throw std::out_of_range("set_levels: load capacitance must be >= 0 pF");
  }
  for (double v : {levels.voh, levels.vol, levels.iol_ma, levels.ioh_ua}) {
    if (!std::isfinite(v)) throw std::out_of_range("set_levels: compare level or load is not finite");
  }
  levels_ = levels;
}

void SimTester::set_timing(int id, const TimingSet& ts) {
  if (id < 0) throw std::invalid_argument("set_timing: id must be >= 0");
  detail::validate_timing_set(spec_, ts);
  timings_[id] = ts;
}

void SimTester::set_temperature(double temp_c) {
  if (powered_) {
    check_range(spec_.abs_max("TA_BIAS"), "TA_BIAS", "TA under bias", temp_c, "C");
  } else {
    check_range(spec_.abs_max("TSTG"), "TSTG", "TA unpowered", temp_c, "C");
  }
  temp_c_ = temp_c;
}

void SimTester::power(bool on) {
  if (on) check_range(spec_.abs_max("TA_BIAS"), "TA_BIAS", "TA under bias", temp_c_, "C");
  powered_ = on;
}

FunctionalResult SimTester::run_pattern(const Pattern& pat) {
  if (!powered_) throw std::logic_error("run_pattern: DUT power is off");

  struct Column {
    int pin;
    spec::Dir dir;
  };
  std::vector<Column> cols;
  for (const auto& name : pat.pins) {
    const spec::Pin& p = spec_.pin(name);
    if (p.dir == spec::Dir::Pwr) throw std::invalid_argument("pattern " + pat.name + ": " + name + " is a supply pin");
    if (name == "CLK") {
      throw std::invalid_argument("pattern " + pat.name + ": CLK is generated from the timing set");
    }
    cols.push_back({p.num, p.dir});
  }
  for (const auto& v : pat.vectors) {
    for (std::size_t i = 0; i < cols.size(); ++i) {
      const char c = v.chars[i];
      const bool input = cols[i].dir == spec::Dir::In;
      const bool output = cols[i].dir == spec::Dir::Out;
      if ((is_drive(c) && output) || (!is_drive(c) && c != 'X' && input)) {
        throw std::invalid_argument("pattern " + pat.name + ": '" + std::string(1, c) +
                                    "' is not valid on " + pat.pins[i] +
                                    (input ? " (input)" : " (output)"));
      }
    }
  }

  std::map<int, ResolvedTiming> resolved;
  const auto timing_for = [&](int id) -> const ResolvedTiming& {
    if (auto it = resolved.find(id); it != resolved.end()) return it->second;
    const auto ts = timings_.find(id);
    const PatternTiming* overlay = nullptr;
    for (const auto& t : pat.timings) {
      if (t.id == id) overlay = &t;
    }
    return resolved
        .emplace(id, detail::resolve_timing(spec_, ts == timings_.end() ? TimingSet{} : ts->second, overlay))
        .first->second;
  };

  Engine eng(spec_, *dut_, electrical_, levels_, temp_c_);
  BusDecoder decoder;
  std::vector<BusTransaction> txs;
  FunctionalResult result;

  for (const Vector& v : pat.vectors) {
    const ResolvedTiming& rt = timing_for(v.timing_id);
    for (uint32_t r = 0; r < v.repeat; ++r) {
      eng.begin_period(rt);
      const int64_t t0 = eng.start();
      for (std::size_t i = 0; i < cols.size(); ++i) {
        const char c = v.chars[i];
        const detail::PinEdges& e = rt.pins[static_cast<std::size_t>(cols[i].pin)];
        if (is_drive(c)) {
          eng.drive(cols[i].pin, t0 + e.drive_ps, c);
          if (e.fmt == DriveFormat::RZ && c == '1') eng.drive(cols[i].pin, t0 + rt.period_ps, '0');
          if (e.fmt == DriveFormat::R1 && c == '0') eng.drive(cols[i].pin, t0 + rt.period_ps, '1');
        } else if (cols[i].dir == spec::Dir::Io) {
          eng.drive(cols[i].pin, t0 + e.drive_ps, 'Z');
        }
      }
      eng.clock();
      for (auto& tx : decoder.end_period(eng)) txs.push_back(tx);

      for (std::size_t i = 0; i < cols.size(); ++i) {
        const char c = v.chars[i];
        if (c != 'L' && c != 'H' && c != 'M') continue;
        const int pin = cols[i].pin;
        const int64_t ts = t0 + rt.pins[static_cast<std::size_t>(pin)].strobe_ps;
        Logic actual = eng.resolved_at(pin, ts);
        bool ok = false;
        if (c == 'M') {
          ok = actual == Logic::Z;
        } else if (actual == Logic::L0 || actual == Logic::L1) {
          const double load = actual == Logic::L0 ? levels_.iol_ma : levels_.ioh_ua / 1000;
          const double volts = electrical_.output_voltage(pat.pins[i], actual, load, eng.conditions());
          actual = volts >= levels_.voh ? Logic::L1 : volts <= levels_.vol ? Logic::L0 : Logic::X;
          ok = (c == 'H' && actual == Logic::L1) || (c == 'L' && actual == Logic::L0);
        }
        if (!ok) result.fails.push_back({eng.index(), pat.pins[i], c, actual, ts});
      }
      eng.end_period();
      ++result.cycles;
    }
  }
  trace_ = eng.finish(std::move(txs));
  result.pass = result.fails.empty();
  return result;
}

// Memory-emulation mode: the tester is the system's RAM and I/O space. It holds RESET for
// kResetClocks, then decodes every bus cycle from the pins, drives read data on the enabled
// byte lanes `drive` ns into T3 (held to the end of T4), captures writes at the WR# rising
// edge and holds READY low for `wait_states` CLKs from T2 (`drive` ns of READY's timing).
FunctionalResult SimTester::run_program(const MemoryImage& image, const RunOpts& opts) {
  if (!powered_) throw std::logic_error("run_program: DUT power is off");
  if (!image.read(kResetVector)) {
    throw std::invalid_argument("run_program: memory image has no code at the reset vector FFFF0h");
  }
  if (opts.wait_states < 0) throw std::invalid_argument("run_program: wait_states must be >= 0");
  if (opts.max_cycles == 0) throw std::invalid_argument("run_program: max_cycles must be > 0");
  for (const PinStimulus& st : opts.stimuli) {
    if (st.pin != "INTR" && st.pin != "NMI" && st.pin != "HOLD" && st.pin != "TEST_N") {
      throw std::invalid_argument("run_program: stimulus pin must be INTR, NMI, HOLD or TEST_N, not " + st.pin);
    }
  }

  const auto ts = timings_.find(opts.timing_id);
  const ResolvedTiming rt =
      detail::resolve_timing(spec_, ts == timings_.end() ? TimingSet{} : ts->second, nullptr);
  const auto drive_at = [&](int pin) { return rt.pins[static_cast<std::size_t>(pin)].drive_ps; };

  Engine eng(spec_, *dut_, electrical_, levels_, temp_c_);
  BusDecoder decoder;
  const auto& bp = eng.bus();
  const int reset = eng.pin_num("RESET"), ready = eng.pin_num("READY"), intr = eng.pin_num("INTR");
  const auto wait_states = static_cast<uint64_t>(opts.wait_states);
  uint64_t last_stimulus = 0;
  for (const PinStimulus& st : opts.stimuli) last_stimulus = std::max(last_stimulus, st.clk);
  uint64_t inta_cycles = 0;
  bool drop_intr = false;

  std::map<uint32_t, uint8_t> mem = image.bytes();
  std::map<uint32_t, uint8_t> io;
  std::vector<BusTransaction> txs;
  std::optional<uint64_t> ready_high_at;
  bool serving = false, release = false, halted = false;
  FunctionalResult result;

  const auto lanes = [](const BusDecoder::Open& o) {
    const bool a0 = (o.address & 1) != 0;
    return std::pair{!a0, !o.bhe_n};  // {low lane D7-D0, high lane D15-D8}
  };

  for (uint64_t k = 0; k < opts.max_cycles; ++k) {
    eng.begin_period(rt);
    const int64_t t0 = eng.start();
    if (k == 0) {
      eng.drive(reset, 0, '1');
      eng.drive(ready, 0, '1');
      eng.drive(eng.pin_num("MN_MX_N"), 0, '1');
      for (const char* name : {"INTR", "NMI", "HOLD", "TEST_N"}) eng.drive(eng.pin_num(name), 0, '0');
    }
    if (k == kResetClocks) eng.drive(reset, t0 + drive_at(reset), '0');
    if (drop_intr) {
      eng.drive(intr, t0 + drive_at(intr), '0');
      drop_intr = false;
    }
    for (const PinStimulus& st : opts.stimuli) {
      if (st.clk != k) continue;
      const int pin = eng.pin_num(st.pin);
      const int64_t at = st.at_ns ? static_cast<int64_t>(std::llround(*st.at_ns * 1000.0)) : drive_at(pin);
      if (at < 0 || at >= rt.period_ps) {
        throw std::invalid_argument("run_program: stimulus at_ns must lie inside its CLK period");
      }
      eng.drive(pin, t0 + at, st.high ? '1' : '0');
    }
    if (release) {
      for (int pin : bp.ad) eng.drive(pin, t0, 'Z');
      release = false;
    }
    if (ready_high_at && *ready_high_at == k) {
      eng.drive(ready, t0 + drive_at(ready), '1');
      ready_high_at.reset();
    }
    if (const auto& o = decoder.open()) {
      if (wait_states > 0 && k == o->t1 + 1) {
        eng.drive(ready, t0 + drive_at(ready), '0');
        ready_high_at = k + 1 + wait_states;  // sampled low at the rising edge of T3 and N-1 Tw
      }
      if (o->kind == BusDecoder::Open::Kind::Read && k == o->t1 + 2) {
        const auto& space = o->m_io ? mem : io;
        const uint32_t mask = o->m_io ? MemoryImage::kAddressMask : kIoAddressMask;
        const auto [lo, hi] = lanes(*o);
        const uint32_t base = o->address & ~1u & mask;
        for (int lane = 0; lane < 2; ++lane) {
          if (!(lane == 0 ? lo : hi)) continue;
          const auto byte = space.find((base + static_cast<uint32_t>(lane)) & mask);
          for (int bit = 0; bit < 8; ++bit) {
            const int pin = bp.ad[static_cast<std::size_t>(lane * 8 + bit)];
            const char c = byte == space.end() ? 'Z' : ((byte->second >> bit) & 1) ? '1' : '0';
            eng.drive(pin, t0 + drive_at(pin), c);
          }
        }
        serving = true;
      }
      if (o->kind == BusDecoder::Open::Kind::Inta && k == o->t1 + 2 && inta_cycles % 2 == 1) {
        for (int bit = 0; bit < 8; ++bit) {
          const int pin = bp.ad[static_cast<std::size_t>(bit)];
          eng.drive(pin, t0 + drive_at(pin), ((opts.inta_type >> bit) & 1) ? '1' : '0');
        }
        serving = true;
      }
    }

    eng.clock();
    for (BusTransaction& tx : decoder.end_period(eng)) {
      using T = model::BusCycleType;
      if (serving && (tx.type == T::MemRead || tx.type == T::CodeFetch || tx.type == T::IoRead ||
                      tx.type == T::InterruptAck)) {
        release = true;
        serving = false;
      }
      if (tx.type == T::InterruptAck && ++inta_cycles % 2 == 0) drop_intr = true;
      if (tx.type == T::MemWrite || tx.type == T::IoWrite) {
        auto& space = tx.type == T::MemWrite ? mem : io;
        const uint32_t mask = tx.type == T::MemWrite ? MemoryImage::kAddressMask : kIoAddressMask;
        const uint32_t base = tx.address & ~1u & mask;
        if ((tx.address & 1) == 0) space[base] = static_cast<uint8_t>(tx.data & 0xFF);
        if (!tx.bhe_n) space[(base + 1) & mask] = static_cast<uint8_t>(tx.data >> 8);
      }
      if (tx.type == T::Halt) halted = true;
      txs.push_back(tx);
    }
    const bool model_halted = eng.bus_state().cycle == model::BusCycleType::Halt;
    const bool stimulus_ahead = !opts.stimuli.empty() && last_stimulus >= k;
    if (model_halted && opts.stop_on_halt && !stimulus_ahead) {
      for (auto& tx : decoder.flush(eng)) txs.push_back(tx);
    }
    halted = (halted || model_halted) && !stimulus_ahead;
    eng.end_period();
    ++result.cycles;
    if (halted && opts.stop_on_halt) break;
  }
  trace_ = eng.finish(std::move(txs));
  result.pass = !opts.stop_on_halt || halted;
  return result;
}

double SimTester::measure_time(const EdgeRef& from, const EdgeRef& to) {
  const auto find = [&](const EdgeRef& ref) {
    const int pin = spec_.pin(ref.pin).num;
    const Logic want = ref.dir == Transition::Rise ? Logic::L1 : Logic::L0;
    uint64_t seen = 0;
    for (const auto& c : trace_.cycles) {
      for (const auto& e : c.events) {
        if (e.pin == pin && e.value == want && seen++ == ref.occurrence) return e.delay_ps;
      }
    }
    throw std::out_of_range("measure_time: " + ref.pin + (ref.dir == Transition::Rise ? " rise" : " fall") +
                            " #" + std::to_string(ref.occurrence) + " not found in last_trace()");
  };
  return static_cast<double>(find(to) - find(from)) / 1000.0;
}

double SimTester::force_i_measure_v(std::string_view pin, double ma) {
  if (!powered_) throw std::logic_error("force_i_measure_v: DUT power is off");
  const spec::Pin& p = spec_.pin(pin);
  if (p.dir != spec::Dir::Out && p.dir != spec::Dir::Io) {
    throw std::invalid_argument("force_i_measure_v: " + p.name + " is not an output");
  }
  const model::Conditions c = static_conditions(levels_, temp_c_, timings_);
  return electrical_.output_voltage(p.name, last_state(trace_, p.num), ma, c);
}

double SimTester::force_v_measure_i(std::string_view pin, double v) {
  if (!powered_) throw std::logic_error("force_v_measure_i: DUT power is off");
  const spec::Pin& p = spec_.pin(pin);
  if (p.dir == spec::Dir::Pwr) throw std::invalid_argument("force_v_measure_i: " + p.name + " is a supply pin");
  check_range(spec_.abs_max("V_PIN"), "V_PIN", "forced " + p.name, v, "V");
  if (p.dir != spec::Dir::In) {
    const Logic s = last_state(trace_, p.num);
    if (s == Logic::L0 || s == Logic::L1) {
      throw std::domain_error("force_v_measure_i: " + p.name + " is driven by the DUT; float it first");
    }
  }
  const model::Conditions c = static_conditions(levels_, temp_c_, timings_);
  return electrical_.leakage_ua(p.name, v, c);
}

double SimTester::measure_icc() {
  if (!powered_) throw std::logic_error("measure_icc: DUT power is off");
  const model::Conditions c = static_conditions(levels_, temp_c_, timings_);
  return electrical_.icc_ma(c);
}

}  // namespace i8086::tester
