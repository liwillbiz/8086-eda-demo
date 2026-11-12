#pragma once

// Minimal stand-in for the M3 device model: runs a fixed script of min-mode bus cycles with
// realistic pin timing, samples READY and read data with setup checks, and logs what it saw.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "i8086/model/device_model.hpp"
#include "i8086/spec/spec.hpp"

namespace i8086::tester::testing {

struct FakeTiming {  // ps after the CLK edge that launches the transition
  int64_t tclav = 30'000;  // address valid after the T1 falling edge
  int64_t tchlh = 15'000;  // ALE rise after the T1 falling edge
  int64_t tchll = 10'000;  // ALE fall after the T1 rising edge
  int64_t tclrl = 20'000;  // RD#/WR# fall after the T2 falling edge
  int64_t tclrh = 15'000;  // RD#/WR# rise after the T4 falling edge
  int64_t tclaz = 10'000;  // AD float after the T2 falling edge (reads)
  int64_t data_setup = 14'000;   // read data must be stable this long before the T4 falling edge
  int64_t ready_setup = 20'000;  // READY must be stable this long before the T3/Tw rising edge
};

struct FakeOp {
  enum class Kind { Fetch, Read, Write, IoWrite, Halt } kind = Kind::Fetch;
  uint32_t address = 0;
  bool word = true;
  uint16_t data = 0;            // write data (as on AD15-0 for a word, or the byte value)
  bool add_last_read = false;   // write data = last read value + data
};

struct FakeLog {
  std::vector<std::optional<uint16_t>> reads;  // nullopt = some enabled bit latched X
  std::vector<int> wait_states;                // Tw count per completed cycle
  std::vector<model::Conditions> conditions;
  uint64_t reset_released_clk = 0;
};

class FakeDevice final : public model::DeviceModel {
 public:
  FakeDevice(const spec::Spec&, std::vector<FakeOp>, std::shared_ptr<FakeLog>, FakeTiming = {});

  void set_conditions(const model::Conditions& c) override { log_->conditions.push_back(c); }
  void set_inputs(std::span<const model::InputSample> in) override;
  std::vector<model::PinEvent> clock_edge(model::Edge) override;
  model::ArchState arch_state() const override { return {}; }
  model::BusState bus_state() const override { return bus_; }

 private:
  void falling(std::vector<model::PinEvent>&);
  void rising(std::vector<model::PinEvent>&);
  void start_cycle(std::vector<model::PinEvent>&);
  void put(std::vector<model::PinEvent>&, int pin, bool high, int64_t delay) const;
  void put_z(std::vector<model::PinEvent>&, int pin, int64_t delay) const;
  bool sampled_high(int pin, int64_t setup, bool& stable) const;

  FakeTiming t_;
  std::vector<FakeOp> ops_;
  std::size_t next_op_ = 0;
  std::shared_ptr<FakeLog> log_;
  std::map<int, model::InputSample> in_;
  model::BusState bus_;
  bool first_edge_ = true, in_reset_ = true, halted_ = false, ready_ = true;
  int countdown_ = -1, tw_ = 0;
  std::optional<uint16_t> last_read_;
  int ale_, rd_n_, wr_n_, den_n_, dt_r_n_, m_io_n_, bhe_n_, inta_n_, reset_, ready_pin_;
  std::vector<int> ad_, a_;
};

}  // namespace i8086::tester::testing
