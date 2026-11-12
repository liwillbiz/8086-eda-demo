#include "fake_device.hpp"

#include <string>

namespace i8086::tester::testing {

using model::BusCycleType;
using model::Logic;
using model::TState;

FakeDevice::FakeDevice(const spec::Spec& s, std::vector<FakeOp> ops, std::shared_ptr<FakeLog> log,
                       FakeTiming t)
    : t_(t),
      ops_(std::move(ops)),
      log_(std::move(log)),
      ale_(s.pin("ALE").num),
      rd_n_(s.pin("RD_N").num),
      wr_n_(s.pin("WR_N").num),
      den_n_(s.pin("DEN_N").num),
      dt_r_n_(s.pin("DT_R_N").num),
      m_io_n_(s.pin("M_IO_N").num),
      bhe_n_(s.pin("BHE_N").num),
      inta_n_(s.pin("INTA_N").num),
      reset_(s.pin("RESET").num),
      ready_pin_(s.pin("READY").num) {
  for (int i = 0; i < 16; ++i) ad_.push_back(s.pin("AD" + std::to_string(i)).num);
  for (int i = 16; i < 20; ++i) a_.push_back(s.pin("A" + std::to_string(i)).num);
}

void FakeDevice::set_inputs(std::span<const model::InputSample> in) {
  for (const auto& s : in) in_[s.pin] = s;
}

void FakeDevice::put(std::vector<model::PinEvent>& ev, int pin, bool high, int64_t delay) const {
  ev.push_back({pin, high ? Logic::L1 : Logic::L0, delay});
}

void FakeDevice::put_z(std::vector<model::PinEvent>& ev, int pin, int64_t delay) const {
  ev.push_back({pin, Logic::Z, delay});
}

bool FakeDevice::sampled_high(int pin, int64_t setup, bool& stable) const {
  const auto it = in_.find(pin);
  if (it == in_.end()) {
    stable = false;
    return false;
  }
  stable = it->second.changed_ps <= -setup &&
           (it->second.value == Logic::L0 || it->second.value == Logic::L1);
  return it->second.value == Logic::L1;
}

std::vector<model::PinEvent> FakeDevice::clock_edge(model::Edge e) {
  std::vector<model::PinEvent> ev;
  if (first_edge_) {
    first_edge_ = false;
    for (int p : {rd_n_, wr_n_, den_n_, inta_n_, dt_r_n_, m_io_n_}) put(ev, p, true, 0);
    put(ev, ale_, false, 0);
    for (int p : ad_) put_z(ev, p, 0);
    for (int p : a_) put_z(ev, p, 0);
    put_z(ev, bhe_n_, 0);
  }
  if (e == model::Edge::Falling) {
    falling(ev);
  } else {
    ++bus_.clk;
    rising(ev);
  }
  return ev;
}

void FakeDevice::rising(std::vector<model::PinEvent>& ev) {
  bool stable = false;
  const bool reset = sampled_high(reset_, 0, stable);
  if (reset) {
    in_reset_ = true;
    countdown_ = -1;
    bus_.t = TState::Ti;
    return;
  }
  if (in_reset_) {
    in_reset_ = false;
    countdown_ = 7;
    log_->reset_released_clk = bus_.clk;
    return;
  }
  if (bus_.t == TState::T1) put(ev, ale_, false, t_.tchll);
  if (bus_.t == TState::T3 || bus_.t == TState::Tw) {
    ready_ = sampled_high(ready_pin_, t_.ready_setup, stable) && stable;
  }
}

void FakeDevice::start_cycle(std::vector<model::PinEvent>& ev) {
  const FakeOp& op = ops_[next_op_];
  const bool odd = (op.address & 1) != 0;
  const bool bhe_n = !(op.word || odd);
  bus_.t = TState::T1;
  bus_.address = op.address & 0xFFFFF;
  bus_.bhe_n = bhe_n;
  switch (op.kind) {
    case FakeOp::Kind::Fetch: bus_.cycle = BusCycleType::CodeFetch; break;
    case FakeOp::Kind::Read: bus_.cycle = BusCycleType::MemRead; break;
    case FakeOp::Kind::Write: bus_.cycle = BusCycleType::MemWrite; break;
    case FakeOp::Kind::IoWrite: bus_.cycle = BusCycleType::IoWrite; break;
    case FakeOp::Kind::Halt: bus_.cycle = BusCycleType::Halt; break;
  }
  for (std::size_t i = 0; i < 16; ++i) put(ev, ad_[i], (op.address >> i) & 1, t_.tclav);
  for (std::size_t i = 0; i < 4; ++i) put(ev, a_[i], (op.address >> (16 + i)) & 1, t_.tclav);
  put(ev, bhe_n_, bhe_n, t_.tclav);
  put(ev, m_io_n_, op.kind != FakeOp::Kind::IoWrite, t_.tclav);
  put(ev, dt_r_n_, op.kind == FakeOp::Kind::Write || op.kind == FakeOp::Kind::IoWrite, t_.tclav);
  put(ev, ale_, true, t_.tchlh);
}

void FakeDevice::falling(std::vector<model::PinEvent>& ev) {
  if (in_reset_ || halted_) return;
  const bool have_op = next_op_ < ops_.size();
  switch (bus_.t) {
    case TState::Ti:
      if (countdown_ > 0) --countdown_;
      if (countdown_ == 0 && have_op) start_cycle(ev);
      return;
    case TState::T1: {
      const FakeOp& op = ops_[next_op_];
      bus_.t = TState::T2;
      if (op.kind == FakeOp::Kind::Halt) {
        halted_ = true;
        return;
      }
      const bool write = op.kind == FakeOp::Kind::Write || op.kind == FakeOp::Kind::IoWrite;
      if (write) {
        uint16_t d = op.data;
        if (op.add_last_read) d = static_cast<uint16_t>(last_read_.value_or(0xDEAD) + d);
        if (!op.word && (op.address & 1)) d = static_cast<uint16_t>(d << 8);
        for (std::size_t i = 0; i < 16; ++i) put(ev, ad_[i], (d >> i) & 1, t_.tclav);
      } else {
        for (int p : ad_) put_z(ev, p, t_.tclaz);
      }
      put(ev, write ? wr_n_ : rd_n_, false, t_.tclrl);
      put(ev, den_n_, false, t_.tclrl);
      return;
    }
    case TState::T2:
      bus_.t = TState::T3;
      tw_ = 0;
      return;
    case TState::T3:
    case TState::Tw:
      if (!ready_) {
        bus_.t = TState::Tw;
        ++tw_;
        return;
      }
      {
        const FakeOp& op = ops_[next_op_];
        bus_.t = TState::T4;
        const bool write = op.kind == FakeOp::Kind::Write || op.kind == FakeOp::Kind::IoWrite;
        if (!write) {
          const bool odd = (op.address & 1) != 0;
          const std::size_t lo = (!op.word && odd) ? 8 : 0, hi = (op.word || odd) ? 16 : 8;
          uint16_t v = 0;
          bool x = false;
          for (std::size_t i = lo; i < hi; ++i) {
            const auto it = in_.find(ad_[i]);
            const bool ok = it != in_.end() && it->second.changed_ps <= -t_.data_setup &&
                            (it->second.value == Logic::L0 || it->second.value == Logic::L1);
            if (!ok) x = true;
            if (ok && it->second.value == Logic::L1) v = static_cast<uint16_t>(v | (1u << (i - lo)));
          }
          last_read_ = x ? std::nullopt : std::optional<uint16_t>(v);
          log_->reads.push_back(last_read_);
        }
        put(ev, write ? wr_n_ : rd_n_, true, t_.tclrh);
        put(ev, den_n_, true, t_.tclrh);
        if (write) {
          for (int p : ad_) put_z(ev, p, t_.tclrh + 10'000);
        }
        log_->wait_states.push_back(tw_);
      }
      return;
    case TState::T4:
      ++next_op_;
      bus_.t = TState::Ti;
      bus_.cycle = BusCycleType::None;
      if (next_op_ < ops_.size()) start_cycle(ev);
      return;
  }
}

}  // namespace i8086::tester::testing
