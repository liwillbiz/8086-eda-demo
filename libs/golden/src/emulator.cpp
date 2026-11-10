#include "i8086/golden/emulator.hpp"

#include "i8086/common/errors.hpp"

namespace i8086::golden {

Registers reset_registers() {
  Registers r;
  r.cs = 0xFFFF;
  return r;
}

Result Emulator::run(const Program&, uint64_t) const {
  throw NotImplemented("golden::Emulator::run [M2]");
}

}  // namespace i8086::golden
