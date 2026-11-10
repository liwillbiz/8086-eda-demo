// Flag tables for the arithmetic group (D1, D2), checked against a reference written with
// plain signed/unsigned integer arithmetic rather than the bit tricks the emulator uses.
// Each program runs one operation per block and reports AL/AX and FLAGS through OUT, so the
// test also exercises PUSHF and I/O capture.

#include <gtest/gtest.h>

#include <bit>
#include <format>
#include <string>

#include "test_util.hpp"

namespace i8086::golden {
namespace {

using namespace test;

enum class Op { Add, Adc, Sub, Sbb, Cmp, Inc, Dec, Neg };

struct Expect {
  uint32_t res;
  uint16_t flags;
};

Expect reference(Op op, int a, int b, bool cin, bool word) {
  const int bits = word ? 16 : 8;
  const int mod = 1 << bits;
  const int smin = -(mod / 2), smax = mod / 2 - 1;
  auto sgn = [&](int v) { return v >= mod / 2 ? v - mod : v; };

  bool cf = cin;
  bool af = false;
  int full = 0, sfull = 0;
  switch (op) {
    case Op::Add:
    case Op::Adc:
    case Op::Inc: {
      const int c = op == Op::Adc && cin ? 1 : 0;
      if (op == Op::Inc) b = 1;
      full = a + b + c;
      sfull = sgn(a) + sgn(b) + c;
      af = (a % 16) + (b % 16) + c > 15;
      if (op != Op::Inc) cf = full >= mod;
      break;
    }
    case Op::Neg:
      b = a;
      a = 0;
      [[fallthrough]];
    default: {
      const int c = op == Op::Sbb && cin ? 1 : 0;
      if (op == Op::Dec) b = 1;
      full = a - b - c;
      sfull = sgn(a) - sgn(b) - c;
      af = (a % 16) - (b % 16) - c < 0;
      if (op != Op::Dec) cf = full < 0;
      break;
    }
  }
  const auto res = static_cast<uint32_t>((full % mod + mod) % mod);
  uint16_t f = 0;
  if (cf) f |= CF;
  if (af) f |= AF;
  if (sfull < smin || sfull > smax) f |= OF;
  if (res == 0) f |= ZF;
  if (res >= static_cast<uint32_t>(mod / 2)) f |= SF;
  if (std::popcount(res & 0xFFu) % 2 == 0) f |= PF;
  return {op == Op::Cmp ? static_cast<uint32_t>(a) : res, f};
}

// One block: STC/CLC; MOV AL/AX,a; <op>; PUSHF; OUT 0,AL/AX; POP AX; OUT 2,AX
void emit(std::vector<uint8_t>& code, Op op, uint16_t a, uint16_t b, bool cin, bool word) {
  const auto lo = static_cast<uint8_t>(a), hi = static_cast<uint8_t>(a >> 8);
  const auto blo = static_cast<uint8_t>(b), bhi = static_cast<uint8_t>(b >> 8);
  code.push_back(cin ? 0xF9 : 0xF8);
  if (word) {
    code.insert(code.end(), {0xB8, lo, hi});
  } else {
    code.insert(code.end(), {0xB0, lo});
  }
  const uint8_t w = word ? 1 : 0;
  switch (op) {
    case Op::Add: code.push_back(static_cast<uint8_t>(0x04 | w)); break;
    case Op::Adc: code.push_back(static_cast<uint8_t>(0x14 | w)); break;
    case Op::Sub: code.push_back(static_cast<uint8_t>(0x2C | w)); break;
    case Op::Sbb: code.push_back(static_cast<uint8_t>(0x1C | w)); break;
    case Op::Cmp: code.push_back(static_cast<uint8_t>(0x3C | w)); break;
    case Op::Inc: code.insert(code.end(), {static_cast<uint8_t>(0xFE | w), 0xC0}); break;
    case Op::Dec: code.insert(code.end(), {static_cast<uint8_t>(0xFE | w), 0xC8}); break;
    case Op::Neg: code.insert(code.end(), {static_cast<uint8_t>(0xF6 | w), 0xD8}); break;
  }
  if (op <= Op::Cmp) {
    code.push_back(blo);
    if (word) code.push_back(bhi);
  }
  code.insert(code.end(), {0x9C, static_cast<uint8_t>(0xE6 | w), 0x00, 0x58, 0xE7, 0x02});
}

std::string op_name(const ::testing::TestParamInfo<Op>& info) {
  static constexpr const char* kNames[] = {"ADD", "ADC", "SUB", "SBB", "CMP", "INC", "DEC", "NEG"};
  return kNames[static_cast<int>(info.param)];
}

struct Case {
  uint16_t a, b;
  bool cin;
};

void check(Op op, const std::vector<Case>& cases, bool word) {
  std::vector<uint8_t> code;
  for (const Case& c : cases) emit(code, op, c.a, c.b, c.cin, word);
  ASSERT_LT(code.size(), 0xFE00u);
  const Result r = run_code(code);
  ASSERT_EQ(r.stop, StopReason::Halt);
  ASSERT_EQ(r.io_writes.size(), 2 * cases.size());
  for (size_t i = 0; i < cases.size(); ++i) {
    const Case& c = cases[i];
    const Expect e = reference(op, c.a, c.b, c.cin, word);
    const IoWrite& value = r.io_writes[2 * i];
    const IoWrite& flags = r.io_writes[2 * i + 1];
    const std::string where = std::format("op={} a={:#x} b={:#x} cin={}", static_cast<int>(op),
                                          c.a, c.b, c.cin);
    ASSERT_EQ(value.value, e.res) << where;
    ASSERT_EQ(flags.value & kArith, e.flags) << where;
    ASSERT_EQ(flags.value & 0xF02A, 0xF002) << where;  // reserved bits as PUSHF stores them
  }
}

class ByteTable : public ::testing::TestWithParam<Op> {};

TEST_P(ByteTable, Exhaustive) {
  const Op op = GetParam();
  if (op >= Op::Inc) {
    std::vector<Case> cases;
    for (int a = 0; a < 256; ++a) {
      for (bool cin : {false, true}) cases.push_back({static_cast<uint16_t>(a), 0, cin});
    }
    check(op, cases, false);
    return;
  }
  for (int a = 0; a < 256; ++a) {
    std::vector<Case> cases;
    for (int b = 0; b < 256; ++b) {
      for (bool cin : {false, true}) {
        cases.push_back({static_cast<uint16_t>(a), static_cast<uint16_t>(b), cin});
      }
    }
    check(op, cases, false);
    if (HasFatalFailure()) return;
  }
}

INSTANTIATE_TEST_SUITE_P(Golden, ByteTable,
                         ::testing::Values(Op::Add, Op::Adc, Op::Sub, Op::Sbb, Op::Cmp, Op::Inc,
                                           Op::Dec, Op::Neg),
                         op_name);

class WordTable : public ::testing::TestWithParam<Op> {};

TEST_P(WordTable, EdgesAndSample) {
  std::vector<Case> cases;
  const uint16_t edges[] = {0x0000, 0x0001, 0x000F, 0x0010, 0x7FFE, 0x7FFF, 0x8000,
                            0x8001, 0x00FF, 0x0100, 0xFF00, 0xFFFE, 0xFFFF};
  for (uint16_t a : edges) {
    for (uint16_t b : edges) {
      for (bool cin : {false, true}) cases.push_back({a, b, cin});
    }
  }
  uint32_t x = 12345;  // fixed-seed LCG
  auto next = [&] {
    x = x * 1103515245u + 12345u;
    return static_cast<uint16_t>(x >> 16);
  };
  while (cases.size() < 4000) {
    const uint16_t a = next(), b = next();
    cases.push_back({a, b, (next() & 1) != 0});
  }
  check(GetParam(), cases, true);
}

INSTANTIATE_TEST_SUITE_P(Golden, WordTable,
                         ::testing::Values(Op::Add, Op::Adc, Op::Sub, Op::Sbb, Op::Cmp, Op::Inc,
                                           Op::Dec, Op::Neg),
                         op_name);

}  // namespace
}  // namespace i8086::golden
