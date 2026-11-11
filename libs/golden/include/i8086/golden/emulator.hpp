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

// External interrupt request (brief §5; M8, additive). It is pending from the boundary
// before instruction `instruction_index` (the count of completed instructions) until it is
// taken: NMI at the next boundary, INTR at the first boundary with IF = 1, NMI first. Taking
// an INTR clears it (8259-style acknowledge). The boundary after STI, MOV SS or POP SS takes
// none. A REP string instruction is one instruction here, so a request that silicon takes
// between iterations is placed at the REP's index. A request not yet due when the CPU
// halts arrives during the halt and ends it if it can be taken; kDuringHalt names that case.
struct InterruptRequest {
  static constexpr uint64_t kDuringHalt = ~uint64_t{0};
  uint64_t instruction_index = 0;
  bool nmi = false;
  uint8_t type = 0;  // INTR: the type byte read in the second INTA cycle
};

struct Program {
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segments;  // physical addr -> bytes
  std::map<uint16_t, uint16_t> io_inputs;                           // value returned by IN per port
  std::vector<InterruptRequest> interrupts;                         // M8, additive
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
