#include "i8086/model/fault_injector.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace i8086::model {

bool FaultSet::has_logic_bug(std::string_view name) const {
  return std::ranges::any_of(all<LogicBug>(), [&](const LogicBug* b) { return b->name == name; });
}

namespace {

// Delays SlowTiming can name: the die timing_ns symbols that place an output edge, plus the
// internal critical path, which make_behavioral applies inside the core.
const std::set<std::string, std::less<>> kDelaySymbols{"TCLAV", "TCLAZ", "TCLDV", "TCLLH", "TCHLL", "TCLRL",
                                                       "TCLRH", "TCVCTV", "TCVCTX", "TCHCTV", "TCLCL_CRIT"};

bool is_write(const BusState& b) { return b.cycle == BusCycleType::MemWrite || b.cycle == BusCycleType::IoWrite; }

}  // namespace

struct FaultInjector::State {
  struct Stuck {
    int pin;
    Logic value;
    uint64_t start_clk;
    bool tristate;   // a 3-state output keeps floating; it is stuck only while driven
    bool input;      // In or Io: the core reads the stuck value
    bool output;     // Out or Io
    bool active = false;
  };
  std::vector<Stuck> stuck;
  struct Slow {
    std::vector<std::string> params;  // empty = every delay
    double scale, add_ps;
  };
  std::vector<Slow> slow;
  std::map<int, int64_t> setup_ps;  // input pin -> extra setup (change seen this much later)
  std::vector<double> vcc_floors;
  double vcc = 5.0;
  uint64_t clk = 0;

  std::map<int, std::string> pin_role;  // output pin -> "AD", "A", "ALE", "RD_N", ...
  std::vector<int> corrupt_pins;        // AD0 and AD8: one bit per byte lane
  std::set<int> corrupted;              // corrupt_pins currently showing the wrong level
  std::map<int, Logic> core_level;      // what the core drives, before any fault

  bool low_vcc() const {
    return std::ranges::any_of(vcc_floors, [&](double f) { return vcc < f; });
  }

  // The die timing symbol that placed `e`, from the edge, the pin, the new level and the
  // bus state after the edge (the core's own choice in core_biu.cpp).
  std::string symbol_of(Edge edge, const PinEvent& e, const BusState& b) const {
    const auto it = pin_role.find(e.pin);
    if (it == pin_role.end()) return {};
    const std::string& r = it->second;
    if (edge == Edge::Rising) {
      if (r == "ALE" && e.value == Logic::L0) return "TCHLL";
      if (r == "M_IO_N" || r == "DT_R_N") return "TCHCTV";
      return "TCVCTX";  // RESET: outputs float or go inactive
    }
    const bool fall = e.value == Logic::L0;
    if (r == "AD") {
      if (e.value == Logic::Z) return "TCLAZ";
      return is_write(b) && b.t == TState::T2 ? "TCLDV" : "TCLAV";
    }
    if (r == "A" || r == "BHE_N") return "TCLAV";
    if (r == "ALE") return "TCLLH";
    if (r == "RD_N") return fall ? "TCLRL" : "TCLRH";
    if (r == "WR_N" || r == "DEN_N") return fall ? "TCVCTV" : "TCVCTX";
    return {};
  }
};

FaultInjector::FaultInjector(const spec::Spec& spec, std::unique_ptr<DeviceModel> inner, FaultSet faults)
    : spec_(spec), inner_(std::move(inner)), faults_(std::move(faults)), state_(std::make_unique<State>()) {
  State& s = *state_;
  for (const auto& p : spec_.pins()) {
    if (p.bus == "AD") s.pin_role[p.num] = "AD";
    else if (p.bus == "A") s.pin_role[p.num] = "A";
    else if (p.dir == spec::Dir::Out) s.pin_role[p.num] = p.name;
  }
  s.corrupt_pins = {spec_.pin("AD0").num, spec_.pin("AD8").num};

  for (const StuckAt* f : faults_.all<StuckAt>()) {
    const spec::Pin& p = spec_.pin(f->pin);
    if (p.dir == spec::Dir::Pwr || p.name == "CLK") {
      throw spec::SpecError("StuckAt: " + p.name + " cannot be stuck (supply or CLK)", "fault", 0);
    }
    s.stuck.push_back({p.num, f->value ? Logic::L1 : Logic::L0, f->start_clk.value_or(0), p.tristate,
                       p.dir == spec::Dir::In || p.dir == spec::Dir::Io,
                       p.dir == spec::Dir::Out || p.dir == spec::Dir::Io});
  }
  for (const SlowTiming* f : faults_.all<SlowTiming>()) {
    for (const auto& sym : f->params) {
      if (!kDelaySymbols.contains(sym)) {
        throw spec::SpecError("SlowTiming: " + sym + " is not a delay the model places", "fault", 0);
      }
    }
    s.slow.push_back({f->params, f->scale, f->add_ns * 1000});
  }
  for (const SetupDegrade* f : faults_.all<SetupDegrade>()) {
    std::vector<int> pins;
    switch (f->input) {
      case InputClass::Data:
        for (const auto& p : spec_.pins()) {
          if (p.bus == "AD") pins.push_back(p.num);
        }
        break;
      case InputClass::Ready:
        pins.push_back(spec_.pin("READY").num);
        break;
      case InputClass::Intr:
        for (const char* n : {"INTR", "NMI", "HOLD"}) pins.push_back(spec_.pin(n).num);
        break;
    }
    for (int p : pins) s.setup_ps[p] += std::llround(f->add_ns * 1000);
  }
  for (const LowVccFail* f : faults_.all<LowVccFail>()) s.vcc_floors.push_back(f->vcc_floor_v);
}

FaultInjector::~FaultInjector() = default;

void FaultInjector::set_conditions(const Conditions& c) {
  state_->vcc = c.vcc_v;
  inner_->set_conditions(c);
}

// SetupDegrade: the core sees a degraded input change later, so it needs that much more
// setup; a change pushed past the edge sits on it (a setup violation). StuckAt: an In or
// Io pin reads the stuck value.
void FaultInjector::set_inputs(std::span<const InputSample> in) {
  const State& s = *state_;
  if (s.setup_ps.empty() && s.stuck.empty()) return inner_->set_inputs(in);
  std::vector<InputSample> out(in.begin(), in.end());
  for (InputSample& x : out) {
    if (const auto it = s.setup_ps.find(x.pin); it != s.setup_ps.end()) {
      x.changed_ps = std::min<int64_t>(0, x.changed_ps + it->second);
    }
    for (const auto& st : s.stuck) {
      if (st.active && st.input && st.pin == x.pin) x.value = st.value;
    }
  }
  inner_->set_inputs(out);
}

std::vector<PinEvent> FaultInjector::clock_edge(Edge e) {
  State& s = *state_;
  if (e == Edge::Rising) ++s.clk;
  std::vector<PinEvent> events = inner_->clock_edge(e);
  const BusState bus = inner_->bus_state();
  for (const PinEvent& ev : events) s.core_level[ev.pin] = ev.value;
  const auto event_on = [&](int pin) -> PinEvent* {
    const auto it = std::ranges::find(events, pin, &PinEvent::pin);
    return it == events.end() ? nullptr : &*it;
  };

  for (PinEvent& ev : events) {
    for (const auto& sl : s.slow) {
      const std::string sym = s.symbol_of(e, ev, bus);
      if (sym.empty() || (!sl.params.empty() && std::ranges::find(sl.params, sym) == sl.params.end())) continue;
      ev.delay_ps = std::llround(static_cast<double>(ev.delay_ps) * sl.scale + sl.add_ps);
    }
  }

  // LowVccFail: below the floor the EU's results are wrong: write data leaves with bit 0 of
  // each byte lane inverted. The core reports only changes, so the injector drives the
  // wrong level itself and restores the core's level when the data phase ends.
  if (e == Edge::Falling) {
    const bool data_phase = is_write(bus) && bus.t != TState::T1 && bus.t != TState::Ti;
    if (s.low_vcc() && is_write(bus) && bus.t == TState::T2) {
      int64_t delay = 0;
      for (const PinEvent& ev : events) {
        if (s.pin_role.contains(ev.pin) && s.pin_role.at(ev.pin) == "AD") delay = std::max(delay, ev.delay_ps);
      }
      for (int pin : s.corrupt_pins) {
        const Logic v = s.core_level[pin];
        if (v != Logic::L0 && v != Logic::L1) continue;
        const Logic wrong = v == Logic::L0 ? Logic::L1 : Logic::L0;
        if (PinEvent* ev = event_on(pin)) ev->value = wrong;
        else events.push_back({pin, wrong, delay});
        s.corrupted.insert(pin);
      }
    } else if (!data_phase && !s.corrupted.empty()) {
      for (int pin : s.corrupted) {
        if (!event_on(pin)) events.push_back({pin, s.core_level[pin], 0});
      }
      s.corrupted.clear();
    }
  }

  for (auto& st : s.stuck) {
    const bool was_active = st.active;
    st.active = s.clk >= st.start_clk;
    if (!st.output) continue;
    for (PinEvent& ev : events) {
      if (ev.pin == st.pin && st.active && (!st.tristate || ev.value != Logic::Z)) ev.value = st.value;
    }
    const auto it = s.core_level.find(st.pin);
    const bool driven = it != s.core_level.end() && it->second != Logic::Z;
    if (!was_active && st.active && !event_on(st.pin) && (!st.tristate || driven)) {
      events.push_back({st.pin, st.value, 0});
    }
  }
  return events;
}

ArchState FaultInjector::arch_state() const { return inner_->arch_state(); }
BusState FaultInjector::bus_state() const { return inner_->bus_state(); }

}  // namespace i8086::model
