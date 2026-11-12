#pragma once

#include <cstdint>
#include <vector>

#include "i8086/golden/emulator.hpp"

namespace i8086::golden::test {

// Flag bits, spelled out independently of the emulator sources.
inline constexpr uint16_t CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040, SF = 0x0080,
                          TF = 0x0100, IF = 0x0200, DF = 0x0400, OF = 0x0800;
inline constexpr uint16_t kArith = OF | SF | ZF | AF | PF | CF;

inline constexpr uint32_t kCodeAddr = 0x00100;  // code runs at 0000:0100

// A reset stub at FFFF0h (JMP FAR 0000:0100) followed by `code` and a trailing HLT.
// The stub is instruction 0, so the first instruction of `code` has index 1.
inline Program boot(std::vector<uint8_t> code) {
  Program p;
  p.segments.push_back({0xFFFF0, {0xEA, 0x00, 0x01, 0x00, 0x00}});
  code.push_back(0xF4);
  p.segments.push_back({kCodeAddr, std::move(code)});
  return p;
}

inline Result run(const Program& p, uint64_t max_instructions = 1'000'000) {
  return Emulator{}.run(p, max_instructions);
}

inline Result run_code(std::vector<uint8_t> code) { return run(boot(std::move(code))); }

}  // namespace i8086::golden::test
