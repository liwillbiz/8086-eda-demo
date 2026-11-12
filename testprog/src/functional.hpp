#pragma once

// Layer-private helpers for memory-emulation tests: run one test ROM on the golden
// emulator and on the DUT (through tester::Tester only), and compare what each wrote to
// I/O and memory. See testprog/roms/inc/i86rom.inc for the ROM conventions.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "i8086/golden/emulator.hpp"
#include "i8086/spec/spec.hpp"
#include "i8086/tester/pattern.hpp"
#include "i8086/tester/tester.hpp"
#include "i8086/testprog/test_case.hpp"

namespace i8086::testprog {

inline constexpr uint16_t kPortData = 0xE0;
inline constexpr uint16_t kPortFlags = 0xE2;
inline constexpr uint32_t kRomTop = 0x100000;  // a ROM's last byte sits at FFFFFh

namespace flag {
inline constexpr uint16_t CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040, SF = 0x0080,
                          TF = 0x0100, IF = 0x0200, DF = 0x0400, OF = 0x0800;
inline constexpr uint16_t kArithmetic = CF | PF | AF | ZF | SF | OF;
}  // namespace flag

// Embedded assets. rom() throws NotImplemented when the build had no NASM, and
// std::invalid_argument for an unknown name.
std::span<const uint8_t> rom(std::string_view name);
tester::Pattern pattern(std::string_view name);

// Timing set 0 for functional tests: the grade's fastest clock (TCLCL min), spec duty cycle,
// read data driven at the start of T3.
tester::TimingSet functional_timing(const spec::Spec&, spec::Grade);
void setup_functional(TestContext&);

struct IoByte {
  uint16_t port = 0;
  uint8_t value = 0;
  bool operator==(const IoByte&) const = default;
};
struct MemByte {
  uint32_t addr = 0;
  uint8_t value = 0;
  bool operator==(const MemByte&) const = default;
};

// What a program made visible outside the CPU, at byte granularity (a word write is its
// low byte, then its high byte), in program order.
struct Observation {
  std::vector<IoByte> io;
  std::vector<MemByte> mem;
  bool halted = false;

  std::vector<uint16_t> words(uint16_t port) const;  // word OUTs to an even port
  std::vector<uint16_t> data() const { return words(kPortData); }
  std::vector<uint16_t> flags() const { return words(kPortFlags); }
};

Observation observe(const golden::Result&);
Observation observe(const tester::BusTrace&, bool halted);

struct DiffOptions {
  // FLAGS bits the golden emulator may legitimately disagree on (design doc §6.1, M2
  // choices). Applied to every word sent to PORT_FLAGS; `per_report` overrides it by index.
  uint16_t undefined_flags = 0;
  std::vector<uint16_t> per_report;
  // Stack bytes are compared by count only: they can hold FLAGS images with undefined
  // bits. The ROM reports anything on the stack worth checking.
  uint32_t stack_lo = 0x00F00, stack_hi = 0x01000;
  std::size_t max_messages = 4;
};

// Empty when the DUT matches the golden prediction.
std::vector<std::string> diff(const Observation& golden, const Observation& dut, const DiffOptions&);

struct RomRun {
  golden::Result golden;
  Observation expected;  // golden
  Observation actual;    // DUT
  tester::FunctionalResult result;
  std::vector<std::string> protocol;  // protocol-checker violations
};

// Runs `rom_name` on ctx.golden and on ctx.t (timing set 0, see setup_functional).
// `interrupts` is the golden view of the external requests in opts.stimuli.
RomRun run_rom(TestContext&, std::string_view rom_name, const tester::RunOpts& = {},
               std::span<const golden::InterruptRequest> interrupts = {});

// Datalog helpers. A functional check is logged as a value judged against [min, max].
spec::Limit exact(double value, std::string ref, std::string unit = "");
Verdict check(TestContext&, std::string param, double value, const spec::Limit&,
              std::string note = {});

std::string hex(uint32_t v, int digits = 4);

}  // namespace i8086::testprog
