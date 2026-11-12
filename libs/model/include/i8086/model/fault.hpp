#pragma once

// Fault descriptions (design doc §4.2). FROZEN INTERFACE (M0).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace i8086::model {

enum class InputClass { Data, Ready, Intr };

struct StuckAt {
  std::string pin;
  int value = 0;  // 0 or 1
  std::optional<uint64_t> start_clk;
};
struct SlowTiming {
  std::vector<std::string> params;  // spec AC symbols; empty = all
  double scale = 1.0;
  double add_ns = 0.0;
};
struct SetupDegrade {
  InputClass input = InputClass::Data;
  double add_ns = 0.0;
};
struct LowVccFail {
  double vcc_floor_v = 0.0;
};
struct Leaky {  // consumed by tester::ElectricalModel, not the logic model
  std::string pin;
  double leakage_ua = 0.0;
};
struct LogicBug {  // consumed inside the behavioral core, e.g. "inc_clears_cf"
  std::string name;
};

using Fault = std::variant<StuckAt, SlowTiming, SetupDegrade, LowVccFail, Leaky, LogicBug>;

struct FaultSet {
  std::vector<Fault> faults;

  bool empty() const { return faults.empty(); }
  bool has_logic_bug(std::string_view name) const;

  template <class T>
  std::vector<const T*> all() const {
    std::vector<const T*> out;
    for (const auto& f : faults) {
      if (const T* p = std::get_if<T>(&f)) out.push_back(p);
    }
    return out;
  }
};

}  // namespace i8086::model
