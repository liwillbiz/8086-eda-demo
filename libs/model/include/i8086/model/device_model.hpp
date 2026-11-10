#pragma once

// Layer 2 — Device model. FROZEN INTERFACE (M0): changes need review from the
// Spec & Integration lead and every affected layer owner (see CODEOWNERS).

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "i8086/model/die_profile.hpp"
#include "i8086/model/fault.hpp"
#include "i8086/spec/spec.hpp"

namespace i8086::model {

enum class Logic : uint8_t { L0, L1, Z, X };
enum class Edge : uint8_t { Rising, Falling };

// Output transition reported for one CLK half-cycle.
struct PinEvent {
  int pin;           // spec pin number
  Logic value;
  int64_t delay_ps;  // relative to the edge passed to clock_edge()
};

struct InputSample {
  int pin;
  Logic value;
  int64_t changed_ps;  // time of the last change, relative to the next clock edge (negative = before)
};

struct Conditions {
  double vcc_v = 5.0;
  double temp_c = 25.0;
  double tclcl_ns = 200.0;
};

enum class TState : uint8_t { Ti, T1, T2, T3, Tw, T4 };
enum class BusCycleType : uint8_t {
  None, CodeFetch, MemRead, MemWrite, IoRead, IoWrite, InterruptAck, Halt
};

struct BusState {
  TState t = TState::Ti;
  BusCycleType cycle = BusCycleType::None;
  uint32_t address = 0;  // 20-bit physical address latched in T1
  bool bhe_n = true;
  uint8_t queue_bytes = 0;
  uint64_t clk = 0;  // full CLK cycles since power-on
};

struct ArchState {
  uint16_t ax = 0, bx = 0, cx = 0, dx = 0;
  uint16_t sp = 0, bp = 0, si = 0, di = 0;
  uint16_t cs = 0, ds = 0, ss = 0, es = 0;
  uint16_t ip = 0, flags = 0;
  std::array<uint8_t, 6> queue{};
  uint8_t queue_len = 0;
  bool halted = false;
};

// Raised for opcodes outside the v1 subset (design doc §7.3). Reported as Error, not Fail.
class UnimplementedInstruction : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class DeviceModel {  // also implemented by VerilatorBackend (M11)
 public:
  virtual ~DeviceModel() = default;
  virtual void set_conditions(const Conditions&) = 0;  // drives derating and LowVccFail
  virtual void set_inputs(std::span<const InputSample>) = 0;
  virtual std::vector<PinEvent> clock_edge(Edge) = 0;
  virtual ArchState arch_state() const = 0;  // debug and white-box unit tests only
  virtual BusState bus_state() const = 0;    // T-state and cycle type, for the protocol checker
};

// Behavioral core wrapped in a FaultInjector carrying `faults` plus die.faults.
std::unique_ptr<DeviceModel> make_behavioral(const spec::Spec&, const DieProfile&, FaultSet);

}  // namespace i8086::model
