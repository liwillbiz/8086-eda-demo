#pragma once

// Layer 1 — Spec as data. FROZEN INTERFACE (M0): changes need review from the
// Spec & Integration lead and every affected layer owner (see CODEOWNERS).

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace i8086::spec {

class SpecError : public std::runtime_error {
 public:
  SpecError(const std::string& msg, std::string file = {}, int line = 0);
  const std::string& file() const { return file_; }
  int line() const { return line_; }

 private:
  std::string file_;
  int line_;
};

struct Limit {
  std::optional<double> min, max;
  std::string unit;
  std::string ref;        // provenance, e.g. "DS §A.C. Characteristics"
  bool verified = false;  // false = value from the brief marked "(verify)"
};

enum class Grade { G8086, G8086_2, G8086_1 };
inline constexpr std::array<Grade, 3> kAllGrades{Grade::G8086, Grade::G8086_2, Grade::G8086_1};
std::string_view to_string(Grade);
Grade parse_grade(std::string_view);  // "8086", "8086-2", "8086-1"; throws SpecError

struct GradeInfo {
  double fclk_max_mhz = 0;
  double tclcl_min_ns = 0;
  double tclcl_max_ns = 0;
};

enum class Dir { In, Out, Io, Pwr };

struct Pin {
  int num = 0;
  std::string name;  // minimum-mode name; active-low pins use an _N suffix
  Dir dir = Dir::In;
  bool tristate = false;
  bool active_low = false;
  std::optional<std::string> max_mode_name;
  std::optional<std::string> bus;  // e.g. "AD"
  std::optional<int> bit;
};

// Brief §3.3. Each table is indexed by the pin bits read MSB-first.
struct DecodeTables {
  std::array<std::string, 8> status;       // [S2 S1 S0] -> bus cycle
  std::array<std::string, 4> segment;      // [S4 S3]    -> segment register
  std::array<std::string, 4> queue;        // [QS1 QS0]  -> queue status
  std::array<std::string, 4> byte_enable;  // [BHE A0]   -> transfer
};

class Spec {
 public:
  static Spec load(const std::filesystem::path&);  // throws SpecError with file:line

  int schema_version() const;
  const std::string& device_name() const;
  const GradeInfo& grade(Grade) const;

  const std::vector<Pin>& pins() const;
  const Pin& pin(std::string_view name) const;  // throws SpecError if unknown

  Limit ac(std::string_view sym, Grade, double tclcl_ns) const;  // evaluates expressions
  Limit dc(std::string_view sym, Grade) const;
  Limit abs_max(std::string_view sym) const;
  const DecodeTables& decode() const;

  struct Impl;  // layer-private; lets M1 change storage without touching this header

 private:
  explicit Spec(std::shared_ptr<const Impl>);
  std::shared_ptr<const Impl> impl_;
};

}  // namespace i8086::spec
