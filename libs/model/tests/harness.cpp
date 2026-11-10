#include "harness.hpp"

#include <cmath>

namespace i8086::model::test {
namespace {
constexpr int64_t kStable = -1'000'000'000;
}

Harness::Harness(FaultSet faults, const std::string& die_file, double tclcl)
    : spec(spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml")),
      die(DieProfile::load(std::string(I86_SOURCE_DIR "/spec/dies/") + die_file)),
      dut(make_behavioral(spec, die, std::move(faults))),
      tclcl_ns(tclcl),
      mem(1U << 20, 0) {
  dut->set_conditions({5.0, 25.0, tclcl_ns});
}

Logic Harness::level(const std::string& name) const {
  const auto it = levels_.find(pin(name));
  return it == levels_.end() ? Logic::X : it->second;
}

void Harness::load(uint32_t addr, const std::vector<uint8_t>& bytes) {
  for (uint8_t b : bytes) mem[addr++ & 0xFFFFF] = b;
}

void Harness::boot(const std::vector<uint8_t>& code) {
  load(0xFFFF0, {0xEA, 0x00, 0x00, 0x00, 0x01});
  std::vector<uint8_t> prog = {0xB8, 0x00, 0x20, 0x8E, 0xD0, 0xBC, 0x00, 0x01, 0xB8, 0x00, 0x00};
  prog.insert(prog.end(), code.begin(), code.end());
  prog.push_back(0xF4);
  load(kCodeBase, prog);
}

uint16_t Harness::ad_levels() const {
  uint16_t v = 0;
  for (int i = 0; i < 16; ++i) {
    if (level("AD" + std::to_string(i)) == Logic::L1) v = static_cast<uint16_t>(v | (1U << i));
  }
  return v;
}

uint16_t Harness::read_word(const BusState& b) const {
  if (b.cycle == BusCycleType::IoRead) {
    const auto port = static_cast<uint16_t>(ale_addr_ & 0xFFFE);
    auto get = [&](uint16_t p) {
      const auto it = io_in.find(p);
      return it == io_in.end() ? uint8_t{0xFF} : it->second;
    };
    return static_cast<uint16_t>(get(port) | (get(static_cast<uint16_t>(port + 1)) << 8));
  }
  const uint32_t a = ale_addr_ & 0xFFFFE;
  return static_cast<uint16_t>(mem[a] | (mem[(a + 1) & 0xFFFFF] << 8));
}

std::vector<InputSample> Harness::inputs_for(Edge next) {
  std::vector<InputSample> s;
  s.push_back({pin("RESET"), reset_high_ ? Logic::L1 : Logic::L0, kStable});
  const BusState b = dut->bus_state();
  const bool reading = b.cycle == BusCycleType::CodeFetch || b.cycle == BusCycleType::MemRead ||
                       b.cycle == BusCycleType::IoRead;
  if (next == Edge::Rising && (state_ == TState::T3 || state_ == TState::Tw)) {
    const bool high = tw_ >= wait_states;
    const int64_t changed = (high && ready_was_low_) ? ready_changed_ps : kStable;
    s.push_back({pin("READY"), high ? Logic::L1 : Logic::L0, changed});
    ready_was_low_ = !high;
    if (high && reading) latch_pending_ = true;
  } else {
    s.push_back({pin("READY"), Logic::L1, kStable});
  }
  if (next == Edge::Falling && latch_pending_) {
    drive_data_ = read_word(b);
    for (int i = 0; i < 16; ++i) {
      s.push_back({pin("AD" + std::to_string(i)), ((drive_data_ >> i) & 1) ? Logic::L1 : Logic::L0,
                   data_changed_ps});
    }
    latch_pending_ = false;
    release_pending_ = true;
  } else if (next == Edge::Rising && release_pending_) {
    const auto low_ps = static_cast<int64_t>(std::llround(tclcl_ns * 1000.0 * 2.0 / 3.0));
    for (int i = 0; i < 16; ++i) {
      s.push_back({pin("AD" + std::to_string(i)), Logic::Z, data_release_ps - low_ps});
    }
    release_pending_ = false;
  }
  for (const auto& [name, v] : force) s.push_back({pin(name), v, kStable});
  return s;
}

void Harness::edge(Edge e) {
  const std::vector<InputSample> in = inputs_for(e);
  dut->set_inputs(in);
  std::vector<PinEvent> ev = dut->clock_edge(e);
  bool ale_fell = false;
  for (const PinEvent& p : ev) {
    levels_[p.pin] = p.value;
    if (p.pin == pin("ALE") && p.value == Logic::L0) ale_fell = true;
  }
  const BusState b = dut->bus_state();
  if (e == Edge::Rising && ale_fell) {
    uint32_t a = ad_levels();
    for (int i = 0; i < 4; ++i) {
      if (level("A" + std::to_string(16 + i)) == Logic::L1) a |= 1U << (16 + i);
    }
    ale_addr_ = a;
    ale_bhe_n_ = level("BHE_N") == Logic::L1;
  }
  if (e == Edge::Falling) {
    const TState prev = state_;
    state_ = b.t;
    if (state_ == TState::T1) {
      tw_ = 0;
      t1_clk_ = b.clk;
      if (b.cycle == BusCycleType::Halt) {
        txns.push_back(Txn{BusCycleType::Halt, b.address, b.bhe_n, 0, 0, 0, t1_clk_});
      }
    }
    if (state_ == TState::Tw) ++tw_;
    if (state_ == TState::T4 && prev != TState::T4) {
      Txn t;
      t.type = b.cycle;
      t.addr = ale_addr_;
      t.bhe_n = ale_bhe_n_;
      t.waits = tw_;
      t.clk = t1_clk_;
      t.s4s3 = static_cast<uint8_t>(((level("A17") == Logic::L1) ? 2 : 0) |
                                    ((level("A16") == Logic::L1) ? 1 : 0));
      const bool write = b.cycle == BusCycleType::MemWrite || b.cycle == BusCycleType::IoWrite;
      const uint16_t v = write ? ad_levels() : drive_data_;
      const bool odd = (t.addr & 1U) != 0;
      t.data = !t.bhe_n && !odd ? v : odd ? static_cast<uint16_t>(v >> 8) : static_cast<uint16_t>(v & 0xFF);
      if (b.cycle == BusCycleType::MemWrite) {
        mem[t.addr] = static_cast<uint8_t>(t.data & 0xFF);
        if (!t.bhe_n && !odd) mem[(t.addr + 1) & 0xFFFFF] = static_cast<uint8_t>(t.data >> 8);
      }
      txns.push_back(t);
    }
  }
  log.push_back(EdgeLog{e, b, std::move(ev)});
}

void Harness::clock() {
  edge(Edge::Falling);
  edge(Edge::Rising);
}

void Harness::reset(int clocks) {
  reset_high_ = true;
  for (int i = 0; i < clocks; ++i) clock();
  reset_high_ = false;
}

bool Harness::run_until_halt(uint64_t max_clocks) {
  for (uint64_t i = 0; i < max_clocks; ++i) {
    clock();
    if (arch().halted && state_ == TState::Ti) return true;
  }
  return false;
}

}  // namespace i8086::model::test
