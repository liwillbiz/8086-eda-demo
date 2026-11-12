#pragma once

// Shmoo, VCC x frequency (design doc §6.5). Layer-private: the CLI's --shmoo and
// ./regress step 4 use it. Each cell runs the functional suite (margin.hpp) on a fresh DUT
// and passes only if every ROM matches the golden emulator.

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include "i8086/spec/spec.hpp"

namespace i8086::testprog {

// spec.yaml `conditions: VCC` (operating range); spec::Spec does not expose conditions.
inline constexpr double kVccOperatingMin = 4.5, kVccOperatingMax = 5.5;

struct ShmooConfig {
  std::filesystem::path spec_path = "spec/spec.yaml";
  std::filesystem::path die_path = "spec/dies/good.yaml";
  double temp_c = 25;
  std::vector<double> vcc_v;  // empty = 4.0 to 6.0 V in 0.25 V steps, clamped to abs_max V_PIN
  std::vector<double> mhz;    // empty = 2 to 12 MHz in 1 MHz steps, then 12.5 (TCLCL 80 ns)
};

struct ShmooCell {
  double vcc_v = 0, mhz = 0;
  bool pass = false;
  std::string failed_rom;  // first failing ROM
  std::string why;
};

struct ShmooBox {
  spec::Grade grade;
  double fmax_mhz = 0;
};

struct ShmooResult {
  std::string die;
  double temp_c = 25;
  std::vector<double> vcc_v;  // ascending
  std::vector<double> mhz;    // ascending
  std::vector<ShmooCell> cells;  // vcc-major: cells[i * mhz.size() + j]
  std::vector<ShmooBox> boxes;   // one per grade, VCC kVccOperatingMin..Max

  const ShmooCell& at(std::size_t vcc_i, std::size_t mhz_j) const { return cells[vcc_i * mhz.size() + mhz_j]; }
  bool in_box(const ShmooCell&, const ShmooBox&) const;
  bool in_any_box(const ShmooCell&) const;
  int box_fails() const;  // failing cells inside any grade's spec box
};

ShmooResult run_shmoo(const ShmooConfig&);

void write_shmoo_csv(std::ostream&, const ShmooResult&);
void write_shmoo_ascii(std::ostream&, const ShmooResult&);
void write_shmoo_svg(std::ostream&, const ShmooResult&);

}  // namespace i8086::testprog
