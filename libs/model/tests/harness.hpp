#pragma once

// Minimal memory-emulation driver for white-box model tests: it acts as the system's
// memory, I/O and READY logic and decodes bus cycles from the pins, like a tester would.
// One CLK = falling edge (starts a T-state) then rising edge. Like SimTester (M4), every
// input is reported at every edge with its value strictly before the edge and the
// absolute time of its last change minus the edge time.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "i8086/model/device_model.hpp"

namespace i8086::model::test {

struct Txn {
  BusCycleType type = BusCycleType::None;
  uint32_t addr = 0;  // decoded from AD/A pins at the ALE falling edge
  bool bhe_n = true;
  uint16_t data = 0;  // lane-extracted byte or word
  int waits = 0;
  uint8_t s4s3 = 0;   // status in T2–T4
  uint64_t clk = 0;   // CLK count when T1 started
};

struct EdgeLog {
  Edge edge;
  BusState bus;  // after the edge
  std::vector<PinEvent> events;
};

class Harness {
 public:
  explicit Harness(FaultSet faults = {}, const std::string& die = "good.yaml",
                   double tclcl_ns = 200.0);

  spec::Spec spec;
  DieProfile die;
  std::unique_ptr<DeviceModel> dut;
  double tclcl_ns;

  std::vector<uint8_t> mem;           // 1 MiB
  std::map<uint16_t, uint8_t> io_in;  // byte-wide I/O read map (missing = FFh)
  std::vector<Txn> txns;
  std::vector<EdgeLog> log;

  // Stimulus knobs.
  double clk_high_ns = 0;               // 0 = TCLCL/3
  int wait_states = 0;
  int64_t data_changed_ps = -50'000;    // read data change time relative to the latching edge
  int64_t data_release_ps = 20'000;     // read data released this long after the latching edge
  int64_t ready_changed_ps = -40'000;   // READY low→high change time relative to the sampling edge
  std::map<std::string, Logic> force;    // extra static inputs (e.g. MN_MX_N)

  int pin(const std::string& name) const { return spec.pin(name).num; }
  Logic level(const std::string& name) const;

  void load(uint32_t addr, const std::vector<uint8_t>& bytes);
  // Far jump at FFFF0h to 0100:0000 (phys 01000h), then SS=2000h, SP=0100h, AX=0, then `code`.
  void boot(const std::vector<uint8_t>& code);
  static constexpr uint32_t kCodeBase = 0x01000;
  static constexpr uint16_t kPrologueLen = 11;

  void reset(int clocks = 4);
  void clock();
  bool run_until_halt(uint64_t max_clocks = 200'000);
  ArchState arch() const { return dut->arch_state(); }

 private:
  void edge(Edge e);
  std::vector<InputSample> inputs_for(Edge next, int64_t edge_t);
  void set(int pin, Logic v, int64_t t);
  uint16_t read_word(const BusState& b) const;
  uint16_t ad_levels() const;

  std::map<int, Logic> levels_;
  struct Drive {
    Logic v;
    int64_t t;  // absolute ps of the last change
  };
  std::map<int, Drive> drive_;
  int64_t period_ps_ = 0, high_ps_ = 0;
  uint64_t periods_ = 0;
  int64_t last_edge_t_ = 0;
  int64_t latch_t_ = 0;
  bool reset_high_ = false;
  TState state_ = TState::Ti;  // after the last falling edge
  bool latch_pending_ = false; // READY given high in T3/Tw: next falling edge latches data
  bool release_pending_ = false;
  int tw_ = 0;
  uint16_t drive_data_ = 0;
  uint32_t ale_addr_ = 0;
  bool ale_bhe_n_ = true;
  uint64_t t1_clk_ = 0;
};

}  // namespace i8086::model::test
