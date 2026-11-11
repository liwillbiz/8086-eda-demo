// Bus interface unit: T-states, pin events, input sampling, prefetch queue.

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "behavioral/core.hpp"
#include "i8086/common/errors.hpp"

namespace i8086::model::detail {
namespace {

constexpr int64_t kStable = std::numeric_limits<int64_t>::min() / 4;
constexpr int64_t kUnset = std::numeric_limits<int64_t>::min();
constexpr std::size_t kQueueSize = 6;
// Brief §5: the first fetch at FFFF0h starts about 7 CLK after RESET falls. RESET is
// sampled on a rising edge, 2/3 CLK after the 8284A drops it on a falling edge.
constexpr int kResetToFirstFetchRising = 6;

bool is_level(Logic v) { return v == Logic::L0 || v == Logic::L1; }
Logic level(bool high) { return high ? Logic::L1 : Logic::L0; }

}  // namespace

DieTiming DieTiming::from(const DieProfile& die) {
  auto get = [&](const char* sym) {
    const auto it = die.timing_ns.find(sym);
    if (it == die.timing_ns.end()) {
      throw spec::SpecError("die profile '" + die.name + "': missing timing_ns." + sym);
    }
    if (!(it->second >= 0.0)) {
      throw spec::SpecError("die profile '" + die.name + "': timing_ns." + sym +
                            " must be >= 0");
    }
    return static_cast<int64_t>(std::llround(it->second * 1000.0));
  };
  return DieTiming{
      .tclav = get("TCLAV"),
      .tclaz = get("TCLAZ"),
      .tcldv = get("TCLDV"),
      .tcllh = get("TCLLH"),
      .tchll = get("TCHLL"),
      .tclrl = get("TCLRL"),
      .tclrh = get("TCLRH"),
      .tcvctv = get("TCVCTV"),
      .tcvctx = get("TCVCTX"),
      .tchctv = get("TCHCTV"),
      .tdvcl = get("TDVCL"),
      .tcldx = get("TCLDX"),
      .tryhch = get("TRYHCH"),
      .tchryx = get("TCHRYX"),
      .tinvch = get("TINVCH"),
      .tclcl_crit = die.timing_ns.contains("TCLCL_CRIT") ? get("TCLCL_CRIT") : 0,
  };
}

PinMap PinMap::from(const spec::Spec& spec) {
  PinMap m{};
  auto num = [&](const char* name) { return spec.pin(name).num; };
  for (int i = 0; i < 16; ++i) m.ad[static_cast<std::size_t>(i)] = num(("AD" + std::to_string(i)).c_str());
  for (int i = 0; i < 4; ++i) m.a[static_cast<std::size_t>(i)] = num(("A" + std::to_string(16 + i)).c_str());
  m.bhe_n = num("BHE_N");
  m.ale = num("ALE");
  m.rd_n = num("RD_N");
  m.wr_n = num("WR_N");
  m.m_io_n = num("M_IO_N");
  m.dt_r_n = num("DT_R_N");
  m.den_n = num("DEN_N");
  m.inta_n = num("INTA_N");
  m.hlda = num("HLDA");
  m.reset = num("RESET");
  m.ready = num("READY");
  m.intr = num("INTR");
  m.nmi = num("NMI");
  m.hold = num("HOLD");
  m.mn_mx_n = num("MN_MX_N");
  for (const spec::Pin& p : spec.pins()) {
    m.max_pin = std::max(m.max_pin, p.num);
    if (p.dir == spec::Dir::Out || p.dir == spec::Dir::Io) m.outputs.push_back(p.num);
  }
  return m;
}

Core::Core(const spec::Spec& spec, const DieProfile& die, FaultSet logic_faults)
    : pins_(PinMap::from(spec)), nominal_(DieTiming::from(die)), timing_(nominal_) {
  // Same keys, defaults and formula as tester::ElectricalModel::derate (design doc §5.2);
  // the tester derates the output delays, the model derates its own input windows.
  auto get = [&](const char* key, double fallback) {
    const auto it = die.electrical.find(key);
    return it == die.electrical.end() ? fallback : it->second;
  };
  derate_ = Derate{get("derate_vnom_v", 5.0), get("derate_vt_v", 1.0), get("derate_alpha", 0.0),
                   get("derate_beta_per_c", 0.0)};
  for (const LogicBug* b : logic_faults.all<LogicBug>()) {
    if (b->name == "inc_clears_cf") {
      bug_inc_clears_cf_ = true;
    } else if (b->name == "shift_count_masked") {
      bug_shift_count_masked_ = true;
    } else {
      throw NotImplemented("LogicBug '" + b->name + "' [M7]");
    }
  }
  const auto n = static_cast<std::size_t>(pins_.max_pin + 1);
  in_.assign(n, In{Logic::L0, kStable});
  raw_.assign(n, In{Logic::Z, kUnset});
  for (int p : pins_.ad) in_[static_cast<std::size_t>(p)].v = Logic::Z;
  in_[static_cast<std::size_t>(pins_.ready)].v = Logic::L1;
  in_[static_cast<std::size_t>(pins_.mn_mx_n)].v = Logic::L1;
  out_.assign(n, Logic::Z);
  enter_reset();
  events_.clear();
}

Core::~Core() = default;

// ---------------------------------------------------------------- pins

void Core::drive(int pin, Logic v, int64_t delay_ps) {
  Logic& cur = out_[static_cast<std::size_t>(pin)];
  if (cur == v) return;
  cur = v;
  events_.push_back(PinEvent{pin, v, delay_ps});
}

void Core::drive_ad(uint16_t value, int64_t delay_ps) {
  for (std::size_t i = 0; i < 16; ++i) drive(pins_.ad[i], level((value >> i) & 1U), delay_ps);
  ad_driven_ = true;
}

void Core::float_ad(int64_t delay_ps) {
  for (int p : pins_.ad) drive(p, Logic::Z, delay_ps);
  ad_driven_ = false;
}

void Core::drive_a(uint8_t nibble, int64_t delay_ps) {
  for (std::size_t i = 0; i < 4; ++i) drive(pins_.a[i], level((nibble >> i) & 1U), delay_ps);
}

Logic Core::sample(int pin, int64_t setup_ps) const {
  const In& i = in_[static_cast<std::size_t>(pin)];
  if (!is_level(i.v) || i.changed_ps > -setup_ps) return Logic::X;
  return i.v;
}

void Core::watch_hold(int pin, Logic* slot, int64_t hold_ps) {
  watches_.push_back(HoldWatch{pin, in_[static_cast<std::size_t>(pin)].v, slot, hold_ps});
}

void Core::set_conditions(const Conditions& c) {
  double k = 1.0 + derate_.beta_per_c * (c.temp_c - 25.0);
  if (derate_.alpha != 0.0) {
    if (c.vcc_v <= derate_.vt_v) {
      throw std::domain_error("behavioral model: VCC " + std::to_string(c.vcc_v) +
                              " V is at or below the die's Vt");
    }
    k *= std::pow((derate_.vnom_v - derate_.vt_v) / (c.vcc_v - derate_.vt_v), derate_.alpha);
  }
  if (!(k > 0)) throw std::domain_error("behavioral model: non-positive derating factor");
  auto scaled = [k](int64_t ps) { return static_cast<int64_t>(std::llround(static_cast<double>(ps) * k)); };
  timing_.tdvcl = scaled(nominal_.tdvcl);
  timing_.tcldx = scaled(nominal_.tcldx);
  timing_.tryhch = scaled(nominal_.tryhch);
  timing_.tchryx = scaled(nominal_.tchryx);
  timing_.tinvch = scaled(nominal_.tinvch);
  timing_.tclcl_crit = scaled(nominal_.tclcl_crit);
  conditions_ = c;
}

double Core::clock_period_ps() const { return conditions_.tclcl_ns * 1000.0; }

// Time from the last edge to the upcoming one. A tester that reports every pin's change
// time at every edge reveals it exactly (an unchanged pin's changed_ps moves by the gap);
// otherwise the model assumes the spec's 33% CLK (low 2/3, high 1/3).
int64_t Core::edge_gap_ps() const {
  if (measured_gap_ps_ > 0) return measured_gap_ps_;
  const double t = conditions_.tclcl_ns * 1000.0;
  return static_cast<int64_t>(std::llround(last_edge_ == Edge::Falling ? t * 2.0 / 3.0 : t / 3.0));
}

void Core::set_inputs(std::span<const InputSample> samples) {
  for (const InputSample& s : samples) {
    if (s.pin <= 0 || s.pin > pins_.max_pin) {
      throw std::out_of_range("set_inputs: pin " + std::to_string(s.pin));
    }
    // A pin that changed and changed back since the last edge shows a smaller gap; take the max.
    const In& raw = raw_[static_cast<std::size_t>(s.pin)];
    if (raw.changed_ps == kUnset || raw.v != s.value || s.changed_ps <= kStable) continue;
    const int64_t gap = raw.changed_ps - s.changed_ps;
    if (gap > 0 && gap < static_cast<int64_t>(conditions_.tclcl_ns * 1000.0)) {
      measured_gap_ps_ = std::max(measured_gap_ps_, gap);
    }
  }
  for (const InputSample& s : samples) {
    for (const HoldWatch& w : watches_) {
      if (w.pin != s.pin || s.value == w.driven) continue;
      const int64_t after_edge = s.changed_ps + edge_gap_ps();
      if (after_edge >= 0 && after_edge < w.hold_ps) *w.slot = Logic::X;
    }
    in_[static_cast<std::size_t>(s.pin)] = In{s.value, s.changed_ps};
    raw_[static_cast<std::size_t>(s.pin)] = In{s.value, s.changed_ps};
  }
}

// ---------------------------------------------------------------- edges

std::vector<PinEvent> Core::clock_edge(Edge e) {
  watches_.clear();
  events_.clear();
  if (!initial_reported_) {
    for (int p : pins_.outputs) events_.push_back(PinEvent{p, out_[static_cast<std::size_t>(p)], 0});
    initial_reported_ = true;
  }
  if (fault_) {
    // A fault is cleared only by RESET high; an undriven RESET (a new run's first edge)
    // neither clears nor re-raises it.
    const Logic reset = in_[static_cast<std::size_t>(pins_.reset)].v;
    if (reset == Logic::L0) std::rethrow_exception(fault_);
    if (reset != Logic::L1) return std::move(events_);
    enter_reset();
  }
  if (e == Edge::Rising) {
    on_rising();
  } else {
    on_falling();
  }
  last_edge_ = e;
  measured_gap_ps_ = 0;
  for (In& i : in_) i.changed_ps = kStable;
  return std::move(events_);
}

void Core::enter_reset() {
  in_reset_ = true;
  startup_ = 0;
  eu_.reset();
  wait_ = Wait::None;
  waiting_ = {};
  fault_ = nullptr;
  eu_req_.reset();
  cur_.reset();
  next_.reset();
  t_ = TState::Ti;
  queue_.clear();
  sregs_ = {0, 0xFFFF, 0, 0};
  ip_ = 0;
  pf_ip_ = 0;
  set_flags_word(0);
  halted_ = false;
  intr_latched_ = false;
  nmi_pending_ = false;
  shadow_ = false;
  hold_req_ = false;
  holding_ = false;

  const int64_t d = timing_.tclaz;
  float_ad(d);
  for (int p : pins_.a) drive(p, Logic::Z, d);
  for (int p : {pins_.bhe_n, pins_.rd_n, pins_.wr_n, pins_.m_io_n, pins_.dt_r_n, pins_.den_n}) {
    drive(p, Logic::Z, d);
  }
  drive(pins_.ale, Logic::L0, d);
  drive(pins_.inta_n, Logic::L1, d);
  drive(pins_.hlda, Logic::L0, d);
}

void Core::release_reset() {
  in_reset_ = false;
  startup_ = kResetToFirstFetchRising;
  const int64_t d = timing_.tcvctx;
  drive(pins_.rd_n, Logic::L1, d);
  drive(pins_.wr_n, Logic::L1, d);
  drive(pins_.den_n, Logic::L1, d);
  eu_ = eu_main();
  waiting_ = eu_.handle();
  wait_ = Wait::Start;
}

void Core::on_rising() {
  ++clk_;
  const Logic reset = in_[static_cast<std::size_t>(pins_.reset)].v;
  if (reset == Logic::L1) {
    if (!in_reset_) enter_reset();
    return;
  }
  if (in_reset_) {
    release_reset();
    return;
  }

  if (in_[static_cast<std::size_t>(pins_.mn_mx_n)].v == Logic::L0) {
    throw NotImplemented("maximum mode (MN/MX# = 0) [M9]");
  }
  // A sample inside the setup window reads X: HOLD keeps its last state, NMI sees no edge
  // and INTR no request until the next edge.
  const Logic hold = sample(pins_.hold, timing_.tinvch);
  if (is_level(hold)) hold_req_ = hold == Logic::L1;
  const Logic nmi = sample(pins_.nmi, timing_.tinvch);
  if (nmi_prev_ == Logic::L0 && nmi == Logic::L1) nmi_pending_ = true;
  if (is_level(nmi)) nmi_prev_ = nmi;
  intr_latched_ = sample(pins_.intr, timing_.tinvch) == Logic::L1;

  switch (t_) {
    case TState::T1:
      drive(pins_.ale, Logic::L0, timing_.tchll);
      break;
    case TState::T3:
    case TState::Tw:
      ready_latch_ = sample(pins_.ready, timing_.tryhch);
      watch_hold(pins_.ready, &ready_latch_, timing_.tchryx);
      break;
    case TState::T4:
      commit_read();
      break;
    default:
      break;
  }

  if (startup_ > 0 && --startup_ > 0) return;
  // Brief §4.4: HOLD is honoured between bus cycles, after the current one ends.
  if (holding_ && !hold_req_) leave_hold();
  if (!holding_ && hold_req_ && (t_ == TState::T4 || t_ == TState::Ti)) enter_hold();
  if (!holding_ && (t_ == TState::T4 || t_ == TState::Ti) && !next_) decide_next();
}

void Core::on_falling() {
  if (in_reset_) return;
  eu_tick();

  switch (t_) {
    case TState::T1: {
      if (cur_->type == BusCycleType::Halt) {
        t_ = TState::Ti;
        halted_ = true;
        eu_done_ = true;
        committed_ = true;
        break;
      }
      t_ = TState::T2;
      const auto s = static_cast<uint8_t>((cur_->status & 0b11) | (flag(0x0200) ? 0b100 : 0));
      drive_a(s, timing_.tclav);
      if (cur_->is_read()) {
        float_ad(timing_.tclaz);
        drive(strobe_of(*cur_), Logic::L0, timing_.tclrl);
      } else {
        if (static_cast<double>(timing_.tclcl_crit) > clock_period_ps()) {
          for (int p : pins_.ad) drive(p, Logic::X, timing_.tcldv);
          ad_driven_ = true;
        } else {
          drive_ad(cur_->wdata, timing_.tcldv);
        }
        drive(pins_.wr_n, Logic::L0, timing_.tcvctv);
      }
      drive(pins_.den_n, Logic::L0, timing_.tcvctv);
      break;
    }
    case TState::T2:
      t_ = TState::T3;
      break;
    case TState::T3:
    case TState::Tw:
      if (ready_latch_ != Logic::L1) {  // low or X: insert a wait state
        t_ = TState::Tw;
        break;
      }
      t_ = TState::T4;
      if (cur_->is_read()) {
        for (std::size_t i = 0; i < 16; ++i) {
          const int p = pins_.ad[i];
          ad_latch_[i] = sample(p, timing_.tdvcl);
          ad_driven_at_latch_[i] = in_[static_cast<std::size_t>(p)].v;
          watch_hold(p, &ad_latch_[i], timing_.tcldx);
        }
        drive(strobe_of(*cur_), Logic::L1, timing_.tclrh);
      } else {
        drive(pins_.wr_n, Logic::L1, timing_.tcvctx);
      }
      drive(pins_.den_n, Logic::L1, timing_.tcvctx);
      break;
    case TState::T4:
    case TState::Ti:
      if (next_ && !holding_) {
        start_t1();
      } else {
        t_ = TState::Ti;
        cur_.reset();
        if (ad_driven_) float_ad(timing_.tclaz);
      }
      break;
  }
}

// ---------------------------------------------------------------- bus cycles

int Core::queue_free() const {
  auto bytes = [](const BusReq& r) { return r.word ? 2 : 1; };
  int used = static_cast<int>(queue_.size());
  if (cur_ && cur_->prefetch && !discard_ && !committed_) used += bytes(*cur_);
  if (next_ && next_->prefetch) used += bytes(*next_);
  return static_cast<int>(kQueueSize) - used;
}

BusReq Core::make_prefetch() const {
  BusReq r;
  r.type = BusCycleType::CodeFetch;
  r.addr = phys(kCS, pf_ip_);
  r.status = kStatusNone;
  r.word = (pf_ip_ & 1U) == 0;
  r.bhe_n = false;  // word at an even address, or the odd byte on D15–D8
  r.prefetch = true;
  return r;
}

void Core::decide_next() {
  if (eu_req_) {
    next_ = *eu_req_;
    eu_req_.reset();
  } else if (!halted_ && queue_free() >= 2) {
    next_ = make_prefetch();
  } else {
    return;
  }
  drive_status(*next_);
}

// M/IO# and DT/R# for the next cycle, valid TCHCTV after CLK high before its T1.
void Core::drive_status(const BusReq& r) {
  if (r.type == BusCycleType::Halt) return;
  const bool memory = r.type == BusCycleType::CodeFetch || r.type == BusCycleType::MemRead ||
                      r.type == BusCycleType::MemWrite;
  drive(pins_.m_io_n, level(memory), timing_.tchctv);
  drive(pins_.dt_r_n, level(!r.is_read()), timing_.tchctv);
}

// HLDA high and every 3-state bus pin floated; ALE stays low. The EU keeps running from the
// queue and waits for any bus cycle it needs.
void Core::enter_hold() {
  holding_ = true;
  drive(pins_.hlda, Logic::L1, timing_.tchctv);
  float_ad(timing_.tclaz);
  for (int p : pins_.a) drive(p, Logic::Z, timing_.tclaz);
  for (int p : {pins_.bhe_n, pins_.rd_n, pins_.wr_n, pins_.m_io_n, pins_.dt_r_n, pins_.den_n}) {
    drive(p, Logic::Z, timing_.tclaz);
  }
}

void Core::leave_hold() {
  holding_ = false;
  drive(pins_.hlda, Logic::L0, timing_.tchctv);
  for (int p : {pins_.rd_n, pins_.wr_n, pins_.den_n}) drive(p, Logic::L1, timing_.tchctv);
  if (next_) drive_status(*next_);
}

void Core::start_t1() {
  cur_ = *next_;
  next_.reset();
  if (cur_->prefetch) {
    cur_ = make_prefetch();
    pf_ip_ = static_cast<uint16_t>(pf_ip_ + (cur_->word ? 2 : 1));
  }
  discard_ = false;
  committed_ = false;
  t_ = TState::T1;
  last_addr_ = cur_->addr;
  last_bhe_n_ = cur_->bhe_n;
  drive(pins_.ale, Logic::L1, timing_.tcllh);
  if (cur_->type == BusCycleType::Halt) return;
  if (cur_->type == BusCycleType::InterruptAck) {
    float_ad(timing_.tclaz);  // no address in an INTA cycle
  } else {
    drive_ad(static_cast<uint16_t>(cur_->addr & 0xFFFF), timing_.tclav);
  }
  drive_a(static_cast<uint8_t>((cur_->addr >> 16) & 0xF), timing_.tclav);
  drive(pins_.bhe_n, level(cur_->bhe_n), timing_.tclav);
}

void Core::commit_read() {
  if (!cur_ || committed_) return;
  committed_ = true;
  uint16_t v = 0;
  if (cur_->is_read()) {
    for (std::size_t i = 0; i < 16; ++i) {
      bool bit = true;  // undriven or X input reads as 1
      switch (ad_latch_[i]) {
        case Logic::L0: bit = false; break;
        case Logic::L1: bit = true; break;
        default:  // setup/hold violation: the latch resolves to the wrong value
          bit = ad_driven_at_latch_[i] != Logic::L1;
          break;
      }
      if (bit) v = static_cast<uint16_t>(v | (1U << i));
    }
  }
  const bool odd = (cur_->addr & 1U) != 0;
  const auto lo = static_cast<uint8_t>(v & 0xFF);
  const auto hi = static_cast<uint8_t>(v >> 8);
  if (cur_->prefetch) {
    if (discard_) return;
    if (cur_->word) {
      queue_.push_back(lo);
      queue_.push_back(hi);
    } else {
      queue_.push_back(odd ? hi : lo);
    }
    return;
  }
  eu_data_ = cur_->word ? v : (odd ? hi : lo);
  eu_done_ = true;
}

void Core::flush_queue() {
  queue_.clear();
  pf_ip_ = ip_;
  if (cur_ && cur_->prefetch && !committed_) discard_ = true;
}

BusState Core::bus_state() const {
  BusState b;
  b.t = t_;
  b.cycle = (cur_ && t_ != TState::Ti) ? cur_->type : BusCycleType::None;
  b.address = last_addr_;
  b.bhe_n = last_bhe_n_;
  b.queue_bytes = static_cast<uint8_t>(queue_.size());
  b.clk = clk_;
  return b;
}

ArchState Core::arch_state() const {
  ArchState a;
  a.ax = regs_[0];
  a.cx = regs_[1];
  a.dx = regs_[2];
  a.bx = regs_[3];
  a.sp = regs_[4];
  a.bp = regs_[5];
  a.si = regs_[6];
  a.di = regs_[7];
  a.es = sregs_[kES];
  a.cs = sregs_[kCS];
  a.ss = sregs_[kSS];
  a.ds = sregs_[kDS];
  a.ip = ip_;
  a.flags = flags_;
  for (std::size_t i = 0; i < queue_.size() && i < a.queue.size(); ++i) a.queue[i] = queue_[i];
  a.queue_len = static_cast<uint8_t>(queue_.size());
  a.halted = halted_;
  return a;
}

}  // namespace i8086::model::detail
