// Bus interface unit: T-states, pin events, input sampling, prefetch queue.

#include <cmath>
#include <limits>
#include <stdexcept>

#include "behavioral/core.hpp"
#include "i8086/common/errors.hpp"

namespace i8086::model::detail {
namespace {

constexpr int64_t kStable = std::numeric_limits<int64_t>::min() / 4;
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
    : pins_(PinMap::from(spec)), timing_(DieTiming::from(die)) {
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

// The model does not see the tester's duty cycle; it assumes the spec's 33% CLK
// (low 2/3, high 1/3) when converting a change time into "time after the last edge".
int64_t Core::half_period_ps(Edge after) const {
  const double t = conditions_.tclcl_ns * 1000.0;
  return static_cast<int64_t>(std::llround(after == Edge::Falling ? t * 2.0 / 3.0 : t / 3.0));
}

void Core::set_inputs(std::span<const InputSample> samples) {
  for (const InputSample& s : samples) {
    if (s.pin <= 0 || s.pin > pins_.max_pin) {
      throw std::out_of_range("set_inputs: pin " + std::to_string(s.pin));
    }
    for (const HoldWatch& w : watches_) {
      if (w.pin != s.pin || s.value == w.driven) continue;
      const int64_t after_edge = s.changed_ps + half_period_ps(last_edge_);
      if (after_edge >= 0 && after_edge < w.hold_ps) *w.slot = Logic::X;
    }
    in_[static_cast<std::size_t>(s.pin)] = In{s.value, s.changed_ps};
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
    if (in_[static_cast<std::size_t>(pins_.reset)].v != Logic::L1) std::rethrow_exception(fault_);
    enter_reset();
  }
  if (e == Edge::Rising) {
    on_rising();
  } else {
    on_falling();
  }
  last_edge_ = e;
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
  if (sample(pins_.hold, timing_.tinvch) == Logic::L1) throw NotImplemented("HOLD/HLDA [M8]");
  const Logic nmi = sample(pins_.nmi, timing_.tinvch);
  if (nmi_prev_ == Logic::L0 && nmi == Logic::L1) throw NotImplemented("NMI [M8]");
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
  if ((t_ == TState::T4 || t_ == TState::Ti) && !next_) decide_next();
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
        drive(pins_.rd_n, Logic::L0, timing_.tclrl);
      } else {
        drive_ad(cur_->wdata, timing_.tcldv);
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
        drive(pins_.rd_n, Logic::L1, timing_.tclrh);
      } else {
        drive(pins_.wr_n, Logic::L1, timing_.tcvctx);
      }
      drive(pins_.den_n, Logic::L1, timing_.tcvctx);
      break;
    case TState::T4:
    case TState::Ti:
      if (next_) {
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
  if (next_->type == BusCycleType::Halt) return;
  const bool memory = next_->type == BusCycleType::CodeFetch ||
                      next_->type == BusCycleType::MemRead ||
                      next_->type == BusCycleType::MemWrite;
  drive(pins_.m_io_n, level(memory), timing_.tchctv);
  drive(pins_.dt_r_n, level(!next_->is_read()), timing_.tchctv);
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
  drive_ad(static_cast<uint16_t>(cur_->addr & 0xFFFF), timing_.tclav);
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
