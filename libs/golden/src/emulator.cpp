#include "i8086/golden/emulator.hpp"

#include "cpu.hpp"

namespace i8086::golden {

Registers reset_registers() {
  Registers r;
  r.cs = 0xFFFF;
  return r;
}

Result Emulator::run(const Program& program, uint64_t max_instructions) const {
  Result result;
  detail::Cpu cpu(program, result);
  cpu.run(max_instructions);
  return result;
}

}  // namespace i8086::golden
