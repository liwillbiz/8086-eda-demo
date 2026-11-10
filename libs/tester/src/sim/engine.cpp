#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace i8086::tester::detail {

using model::Logic;

namespace {

constexpr int64_t kNeverChanged = std::numeric_limits<int64_t>::min() / 4;

bool same(const model::Conditions& a, const model::Conditions& b) {
  return a.vcc_v == b.vcc_v && a.temp_c == b.temp_c && a.tclcl_ns == b.tclcl_ns;
}

}  // namespace

void Timeline::set(int64_t t, Logic v) {
  auto it = std::ranges::upper_bound(changes_, t, {}, &Change::t);
  if (it != changes_.begin() && std::prev(it)->t == t) {
    std::prev(it)->v = v;
  } else {
    changes_.insert(it, Change{t, v});
  }
}

Logic Timeline::at(int64_t t) const {
  auto it = std::ranges::upper_bound(changes_, t, {}, &Change::t);
  return it == changes_.begin() ? initial_ : std::prev(it)->v;
}

const Change* Timeline::last_before(int64_t t) const {
  auto it = std::ranges::lower_bound(changes_, t, {}, &Change::t);
  return it == changes_.begin() ? nullptr : &*std::prev(it);
}

Logic resolve(Logic tester, Logic dut) {
  if (tester == Logic::Z) return dut;
  if (dut == Logic::Z) return tester;
  return tester == dut ? dut : Logic::X;  // contention
}

BusPins::BusPins(const spec::Spec& s)
    : ale(s.pin("ALE").num),
      rd_n(s.pin("RD_N").num),
      wr_n(s.pin("WR_N").num),
      inta_n(s.pin("INTA_N").num),
      m_io_n(s.pin("M_IO_N").num),
      dt_r_n(s.pin("DT_R_N").num),
      bhe_n(s.pin("BHE_N").num) {
  for (int i = 0; i < 16; ++i) ad.push_back(s.pin("AD" + std::to_string(i)).num);
  for (int i = 16; i < 20; ++i) a.push_back(s.pin("A" + std::to_string(i)).num);
}

Engine::Engine(const spec::Spec& spec, model::DeviceModel& dut, const ElectricalModel& electrical,
               const LevelSet& levels, double temp_c)
    : spec_(spec),
      dut_model_(dut),
      electrical_(electrical),
      levels_(levels),
      temp_c_(temp_c),
      bus_(spec),
      clk_pin_(spec.pin("CLK").num) {
  int max_num = 0;
  for (const auto& p : spec.pins()) max_num = std::max(max_num, p.num);
  by_num_.assign(idx(max_num) + 1, nullptr);
  tester_.assign(idx(max_num) + 1, Timeline(Logic::Z));
  dut_.assign(idx(max_num) + 1, Timeline(Logic::Z));
  for (const auto& p : spec.pins()) {
    by_num_[idx(p.num)] = &p;
    if (p.dir == spec::Dir::Out && !p.tristate) dut_[idx(p.num)] = Timeline(Logic::X);
    if ((p.dir == spec::Dir::In || p.dir == spec::Dir::Io) && p.num != clk_pin_) {
      input_pins_.push_back(p.num);
    }
  }
}

void Engine::begin_period(const ResolvedTiming& rt) {
  timing_ = &rt;
  cond_ = model::Conditions{levels_.vcc, temp_c_, rt.period_ns};
  if (!sent_cond_ || !same(*sent_cond_, cond_)) {
    dut_model_.set_conditions(cond_);
    sent_cond_ = cond_;
    derate_ = electrical_.derate(cond_);
  }
}

void Engine::drive(int pin, int64_t t, char c) {
  Logic v = Logic::Z;
  if (c == '0' || c == '1') {
    v = electrical_.input_level(pin_name(pin), c == '1' ? levels_.vih : levels_.vil, cond_);
  } else if (c != 'Z') {
    throw std::logic_error(std::string("Engine::drive: bad drive char ") + c);
  }
  tester_[idx(pin)].set(t, v);
}

std::string_view Engine::pin_name(int num) const { return pin(num).name; }

void Engine::edge(model::Edge e, int64_t t) {
  std::vector<model::InputSample> in;
  in.reserve(input_pins_.size());
  for (int p : input_pins_) {
    const Timeline& tl = tester_[idx(p)];
    const Change* c = tl.last_before(t);
    in.push_back({p, c ? c->v : tl.initial(), c ? c->t - t : kNeverChanged});
  }
  dut_model_.set_inputs(in);
  for (const model::PinEvent& ev : dut_model_.clock_edge(e)) {
    if (ev.pin <= 0 || idx(ev.pin) >= by_num_.size() || !by_num_[idx(ev.pin)]) {
      throw std::logic_error("DeviceModel reported an event on unknown pin " + std::to_string(ev.pin));
    }
    const spec::Pin& p = pin(ev.pin);
    if (p.dir != spec::Dir::Out && p.dir != spec::Dir::Io) {
      throw std::logic_error("DeviceModel drove non-output pin " + p.name);
    }
    const double delay = static_cast<double>(std::max<int64_t>(0, ev.delay_ps)) * derate_;
    dut_[idx(ev.pin)].set(t + std::llround(delay), ev.value);
  }
  tester_[idx(clk_pin_)].set(t, e == model::Edge::Rising ? Logic::L1 : Logic::L0);
}

void Engine::clock() {
  edge(model::Edge::Falling, start_);
  edge(model::Edge::Rising, start_ + timing_->period_ps - timing_->high_ps);
  periods_.push_back({start_, timing_->period_ps, dut_model_.bus_state()});
}

void Engine::end_period() {
  start_ += timing_->period_ps;
  ++index_;
}

Logic Engine::resolved_at(int pin, int64_t t) const {
  return resolve(tester_[idx(pin)].at(t), dut_[idx(pin)].at(t));
}

BusTrace Engine::finish(std::vector<BusTransaction> transactions) const {
  BusTrace trace;
  trace.transactions = std::move(transactions);
  trace.cycles.resize(periods_.size());
  for (std::size_t k = 0; k < periods_.size(); ++k) {
    trace.cycles[k].clk = k;
    trace.cycles[k].bus = periods_[k].bus;
  }
  if (periods_.empty()) return trace;

  const auto cycle_of = [&](int64_t t) {
    auto it = std::ranges::upper_bound(periods_, t, {}, &Period::start);
    return it == periods_.begin() ? std::size_t{0}
                                  : static_cast<std::size_t>(std::prev(it) - periods_.begin());
  };
  for (const auto* p : by_num_) {
    if (!p || p->dir == spec::Dir::Pwr) continue;
    const Timeline& a = tester_[idx(p->num)];
    const Timeline& b = dut_[idx(p->num)];
    std::vector<int64_t> times;
    for (const auto& c : a.changes()) times.push_back(c.t);
    for (const auto& c : b.changes()) times.push_back(c.t);
    std::ranges::sort(times);
    times.erase(std::unique(times.begin(), times.end()), times.end());
    Logic prev = resolve(a.initial(), b.initial());
    for (int64_t t : times) {
      const Logic v = resolve(a.at(t), b.at(t));
      if (v == prev) continue;
      prev = v;
      trace.cycles[cycle_of(t)].events.push_back({p->num, v, t});
    }
  }
  for (auto& c : trace.cycles) {
    std::ranges::sort(c.events, [](const model::PinEvent& x, const model::PinEvent& y) {
      return x.delay_ps != y.delay_ps ? x.delay_ps < y.delay_ps : x.pin < y.pin;
    });
  }
  return trace;
}

// ---------------------------------------------------------------------------------------------

uint16_t sample_ad(const Engine& eng, int64_t t) {
  uint16_t v = 0;
  for (std::size_t i = 0; i < eng.bus().ad.size(); ++i) {
    if (eng.resolved_at(eng.bus().ad[i], t) == Logic::L1) v = static_cast<uint16_t>(v | (1u << i));
  }
  return v;
}

std::vector<BusTransaction> BusDecoder::end_period(const Engine& eng) {
  const BusPins& bp = eng.bus();
  const uint64_t k = eng.index();
  const int64_t from = eng.start(), to = eng.start() + eng.timing().period_ps;

  struct Ev {
    int64_t t;
    int pin;
    Logic v;
  };
  std::vector<Ev> evs;
  for (int pin : {bp.ale, bp.rd_n, bp.wr_n, bp.inta_n}) {
    const Timeline& tl = eng.dut_timeline(pin);
    for (const Change& c : tl.changes()) {
      if (c.t >= from && c.t < to && tl.at(c.t - 1) != c.v) evs.push_back({c.t, pin, c.v});
    }
  }
  std::ranges::sort(evs, {}, &Ev::t);

  std::vector<BusTransaction> closed;
  for (const Ev& e : evs) {
    if (e.pin == bp.ale) {
      if (e.v == Logic::L1) last_ale_rise_ = k;
      if (e.v != Logic::L0 || eng.dut_timeline(bp.ale).at(e.t - 1) != Logic::L1) continue;
      if (open_) closed.push_back(close(eng, k, -1));
      Open o;
      o.t1 = last_ale_rise_;
      for (std::size_t i = 0; i < bp.ad.size(); ++i) {
        if (eng.resolved_at(bp.ad[i], e.t) == Logic::L1) o.address |= 1u << i;
      }
      for (std::size_t i = 0; i < bp.a.size(); ++i) {
        if (eng.resolved_at(bp.a[i], e.t) == Logic::L1) o.address |= 1u << (16 + i);
      }
      o.bhe_n = eng.resolved_at(bp.bhe_n, e.t) != Logic::L0;
      o.m_io = eng.resolved_at(bp.m_io_n, e.t) != Logic::L0;
      o.hint = eng.bus_state_at(o.t1).cycle;
      open_ = o;
      continue;
    }
    if (!open_) continue;
    using Kind = Open::Kind;
    const Kind kind = e.pin == bp.rd_n ? Kind::Read : e.pin == bp.wr_n ? Kind::Write : Kind::Inta;
    if (e.v == Logic::L0 && open_->kind == Kind::Unknown) {
      open_->kind = kind;
    } else if (e.v == Logic::L1 && open_->kind == kind) {
      closed.push_back(close(eng, k, e.t));
    }
  }
  if (open_ && open_->kind == Open::Kind::Unknown && k >= open_->t1 + 2) {
    closed.push_back(close(eng, k, -1));
  }
  return closed;
}

std::vector<BusTransaction> BusDecoder::flush(const Engine& eng) {
  if (!open_ || open_->kind != Open::Kind::Unknown) return {};
  return {close(eng, eng.index(), -1)};
}

BusTransaction BusDecoder::close(const Engine& eng, uint64_t k, int64_t strobe_rise_t) {
  const Open o = *open_;
  open_.reset();
  BusTransaction tx;
  tx.address = o.address;
  tx.bhe_n = o.bhe_n;
  tx.start_clk = o.t1;
  using Kind = Open::Kind;
  using T = model::BusCycleType;
  switch (o.kind) {
    case Kind::Read:
      tx.type = !o.m_io ? T::IoRead : o.hint == T::CodeFetch ? T::CodeFetch : T::MemRead;
      // Read data is latched by the CLK falling edge that starts T4.
      tx.data = sample_ad(eng, eng.period_start(k) - 1);
      break;
    case Kind::Write:
      tx.type = o.m_io ? T::MemWrite : T::IoWrite;
      tx.data = sample_ad(eng, strobe_rise_t - 1);
      break;
    case Kind::Inta:
      tx.type = T::InterruptAck;
      tx.data = sample_ad(eng, eng.period_start(k) - 1);
      break;
    case Kind::Unknown:
      tx.type = T::Halt;
      break;
  }
  if (o.kind != Kind::Unknown && k >= o.t1 + 3) tx.wait_states = static_cast<int>(k - o.t1 - 3);
  return tx;
}

}  // namespace i8086::tester::detail
