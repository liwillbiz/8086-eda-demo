#pragma once

// Simulated silicon (spec/dies/*.yaml, design doc §2.1). FROZEN INTERFACE (M0).
// Read only by the device model and tester::ElectricalModel, never by test cases.

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "i8086/model/fault.hpp"

namespace i8086::model {

struct DieProfile {
  std::string name;
  std::map<std::string, double, std::less<>> timing_ns;   // nominal delays, setup/hold by spec symbol
  std::map<std::string, double, std::less<>> electrical;  // thresholds, R_on, derating alpha/beta, ...
  FaultSet faults;

  static DieProfile load(const std::filesystem::path&);  // throws spec::SpecError
};

}  // namespace i8086::model
