#pragma once

// Synthetic analog behavior (design doc §5.2). FROZEN INTERFACE (M0).

#include <cstdint>
#include <string_view>

#include "i8086/model/device_model.hpp"
#include "i8086/model/die_profile.hpp"

namespace i8086::tester {

class ElectricalModel {
 public:
  explicit ElectricalModel(model::DieProfile die);

  // k(VCC,T) multiplier applied to every model delay and setup requirement.
  double derate(const model::Conditions&) const;
  // How an input pin reads a driven voltage (die thresholds; between them = X).
  model::Logic input_level(std::string_view pin, double driven_v, const model::Conditions&) const;
  // Output voltage for a driven logic level under load (mA, + = sink).
  double output_voltage(std::string_view pin, model::Logic, double load_ma,
                        const model::Conditions&) const;
  double leakage_ua(std::string_view pin, double applied_v, const model::Conditions&) const;
  double icc_ma(const model::Conditions&) const;
  int64_t load_delay_ps(std::string_view pin, double cl_pf) const;

  const model::DieProfile& die() const { return die_; }

 private:
  model::DieProfile die_;
};

}  // namespace i8086::tester
