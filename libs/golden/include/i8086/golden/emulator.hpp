#pragma once

// Golden reference: instruction-level 8086 emulator, no bus timing.
// FROZEN INTERFACE (M0). Must never depend on or share code with libs/model;
// the build enforces this by linking golden against i86::common only.

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace i8086::golden {

struct Registers {
  uint16_t ax = 0, bx = 0, cx = 0, dx = 0;
  uint16_t sp = 0, bp = 0, si = 0, di = 0;
  uint16_t cs = 0, ds = 0, ss = 0, es = 0;
  uint16_t ip = 0, flags = 0;
};

struct MemWrite {
  uint32_t addr = 0;
  uint8_t value = 0;
};
struct IoWrite {
  uint16_t port = 0;
  uint16_t value = 0;
  bool word = false;
};
struct InterruptEntry {
  uint8_t type = 0;
  uint64_t instruction_index = 0;
};

struct Program {
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segments;  // physical addr -> bytes
  std::map<uint16_t, uint16_t> io_inputs;                           // value returned by IN per port
};

enum class StopReason { Halt, InstructionLimit, Unimplemented };

struct Result {
  Registers regs;
  std::vector<MemWrite> mem_writes;
  std::vector<IoWrite> io_writes;
  std::vector<InterruptEntry> interrupts;
  uint64_t instructions = 0;
  StopReason stop = StopReason::Halt;
};

// Reset state per brief §5: CS=FFFFh, IP=0, flags clear, queue empty.
Registers reset_registers();

class Emulator {
 public:
  Result run(const Program&, uint64_t max_instructions = 1'000'000) const;
};

}  // namespace i8086::golden
