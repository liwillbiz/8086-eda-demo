#pragma once

// Base for memory-emulation tests that run a test ROM on the DUT and on the golden emulator.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "functional.hpp"
#include "i8086/testprog/test_case.hpp"

namespace i8086::testprog {

Verdict worst(Verdict a, Verdict b);

// Logs `param` = actual & care, judged against expected & care. A missing value fails.
Verdict expect_word(TestContext&, std::string param, std::optional<uint16_t> actual,
                    uint16_t expected, std::string ref, uint16_t care = 0xFFFF);

// The i-th element, if present.
std::optional<uint16_t> nth(const std::vector<uint16_t>&, std::size_t i);

// Runs rom(), then judges, in order: the DUT halted, the bus protocol, the DUT against the
// golden emulator (undefined flags masked), and the brief's own expected values.
class RomCase : public TestCase {
 public:
  Verdict run(TestContext&) override;

 protected:
  virtual std::string_view rom_name() const = 0;
  virtual DiffOptions diff_options() const { return {}; }
  // Checks written from the brief, independent of the golden emulator (design doc §14).
  virtual Verdict brief_checks(TestContext&, const RomRun&) = 0;
  std::string ref() const { return "brief §7 " + std::string(id()); }
};

}  // namespace i8086::testprog
