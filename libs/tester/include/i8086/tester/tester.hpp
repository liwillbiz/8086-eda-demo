#pragma once

// Layer 3 — Abstract ATE-like tester. FROZEN INTERFACE (M0): changes need review
// from the Spec & Integration lead and every affected layer owner (see CODEOWNERS).
// Test cases see only this API, never model::DeviceModel.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "i8086/model/device_model.hpp"
#include "i8086/tester/memory_image.hpp"
#include "i8086/tester/pattern.hpp"

namespace i8086::tester {

struct LevelSet {
  double vcc = 5.0;
  double vih = 2.4, vil = 0.45;  // drive levels
  double voh = 1.5, vol = 1.5;   // compare thresholds
  double iol_ma = 2.5, ioh_ua = -400.0;
  double vch = 3.9, vcl = 0.6;   // CLK drive levels (M6, additive)
  double cl_pf = 0.0;            // load capacitance on every output (M6, additive)
};

enum class DriveFormat { NRZ, RZ, R1 };

struct PinTiming {
  DriveFormat fmt = DriveFormat::NRZ;
  double drive_ns = 0.0;   // into the period
  double strobe_ns = 0.0;  // compare edge into the period
};

struct TimingSet {
  double period_ns = 200.0;  // TCLCL
  double clk_high_ns = 0.0;  // 0 = spec default duty
  std::map<std::string, PinTiming, std::less<>> pins;
};

struct VectorFail {
  uint64_t vector = 0;
  std::string pin;
  char expected = 'X';  // pattern compare char: L H M
  model::Logic actual = model::Logic::X;
  int64_t strobe_ps = 0;
};

struct FunctionalResult {
  bool pass = false;
  std::vector<VectorFail> fails;
  uint64_t cycles = 0;
};

// Per-CLK pin log plus decoded bus transactions, for protocol checkers and measure_time().
struct TraceCycle {
  uint64_t clk = 0;
  model::BusState bus;
  std::vector<model::PinEvent> events;
};
struct BusTransaction {
  model::BusCycleType type = model::BusCycleType::None;
  uint32_t address = 0;
  uint16_t data = 0;
  bool bhe_n = true;
  uint64_t start_clk = 0;
  int wait_states = 0;
};
struct BusTrace {
  std::vector<TraceCycle> cycles;
  std::vector<BusTransaction> transactions;
};

// M8, additive: one level change on an asynchronous input (INTR, NMI, HOLD or TEST_N) in
// run_program. It applies in CLK `clk` (0 = first CLK of the run, RESET included), at `at_ns`
// after that CLK's falling edge, or at the pin's timing-set drive time when unset.
struct PinStimulus {
  std::string pin;
  bool high = true;
  uint64_t clk = 0;
  std::optional<double> at_ns;
};

// Memory-emulation mode options.
struct RunOpts {
  int timing_id = 0;
  uint64_t max_cycles = 1'000'000;
  int wait_states = 0;  // READY held low for this many Tw per bus cycle
  // With stimuli, only a halt seen in a CLK after the last stimulus stops the run; a stimulus
  // meant to wake the CPU must come at least 3 CLKs after the halt.
  bool stop_on_halt = true;
  // M8, additive. Like an 8259: the tester drives `inta_type` on AD7-AD0 in the second of
  // each pair of INTA cycles and drops INTR after it.
  std::vector<PinStimulus> stimuli;
  uint8_t inta_type = 0;
};

enum class Transition { Rise, Fall };
struct EdgeRef {
  std::string pin;  // spec pin name, or "CLK"
  Transition dir = Transition::Rise;
  uint64_t occurrence = 0;  // 0 = first such edge in last_trace()
};

class Tester {
 public:
  virtual ~Tester() = default;
  virtual void set_levels(const LevelSet&) = 0;  // throws if beyond abs_max
  virtual void set_timing(int id, const TimingSet&) = 0;
  virtual void set_temperature(double temp_c) = 0;  // thermal head / handler setpoint
  virtual void power(bool on) = 0;
  virtual FunctionalResult run_pattern(const Pattern&) = 0;  // vector mode
  virtual FunctionalResult run_program(const MemoryImage&, const RunOpts&) = 0;  // memory emulation
  virtual const BusTrace& last_trace() const = 0;
  virtual double measure_time(const EdgeRef& from, const EdgeRef& to) = 0;  // ns
  virtual double force_i_measure_v(std::string_view pin, double ma) = 0;   // V
  virtual double force_v_measure_i(std::string_view pin, double v) = 0;    // µA
  virtual double measure_icc() = 0;                                        // mA
};

}  // namespace i8086::tester
