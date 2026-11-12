#include "i8086/tester/electrical_model.hpp"

#include "i8086/common/errors.hpp"

namespace i8086::tester {

ElectricalModel::ElectricalModel(model::DieProfile die) : die_(std::move(die)) {}

// M0 stub: ideal part. M6 replaces each method with the §5.2 model.
double ElectricalModel::derate(const model::Conditions&) const { return 1.0; }

model::Logic ElectricalModel::input_level(std::string_view, double driven_v,
                                          const model::Conditions&) const {
  return driven_v >= 1.5 ? model::Logic::L1 : model::Logic::L0;
}

double ElectricalModel::output_voltage(std::string_view, model::Logic level, double,
                                       const model::Conditions& c) const {
  switch (level) {
    case model::Logic::L0: return 0.0;
    case model::Logic::L1: return c.vcc_v;
    default: throw NotImplemented("ElectricalModel::output_voltage for Z/X [M6]");
  }
}

double ElectricalModel::leakage_ua(std::string_view, double, const model::Conditions&) const {
  return 0.0;
}

double ElectricalModel::icc_ma(const model::Conditions&) const {
  throw NotImplemented("ElectricalModel::icc_ma [M6]");
}

int64_t ElectricalModel::load_delay_ps(std::string_view, double) const { return 0; }

}  // namespace i8086::tester
