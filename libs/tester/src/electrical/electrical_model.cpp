#include "i8086/tester/electrical_model.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace i8086::tester {
namespace {

// Die keys (spec/dies/*.yaml `electrical:`). "KEY.PIN" overrides "KEY" for one pin.
// A missing key gives an ideal part: no offsets, no resistance, no derating, no leakage.
constexpr std::string_view kVinThreshold = "vin_threshold_v";
constexpr std::string_view kVinBand = "vin_band_v";
constexpr std::string_view kVolOffset = "vol_offset_v";
constexpr std::string_view kVohDrop = "voh_drop_v";
constexpr std::string_view kRonLow = "ron_low_ohm";
constexpr std::string_view kRonHigh = "ron_high_ohm";
constexpr std::string_view kLeak = "leak_ua";
constexpr std::string_view kLoadSlope = "load_slope_ns_per_pf";
constexpr std::string_view kDerateVnom = "derate_vnom_v";
constexpr std::string_view kDerateVt = "derate_vt_v";
constexpr std::string_view kDerateAlpha = "derate_alpha";
constexpr std::string_view kDerateBeta = "derate_beta_per_c";
constexpr std::string_view kIccBase = "icc_base_ma";
constexpr std::string_view kIccPerV = "icc_ma_per_v";
constexpr std::string_view kIccPerC = "icc_ma_per_c";

// The ideal input switches at the AC timing reference level (spec conditions.AC_VREF).
constexpr double kIdealThresholdV = 1.5;
constexpr double kDerateRefTempC = 25.0;
constexpr double kNominalVccV = 5.0;  // brief §6.2: VCC 5 V ±10%

double get(const model::DieProfile& die, std::string_view key, double fallback) {
  const auto it = die.electrical.find(key);
  return it == die.electrical.end() ? fallback : it->second;
}

double get_pin(const model::DieProfile& die, std::string_view key, std::string_view pin,
               double fallback) {
  const std::string k = std::string(key) + "." + std::string(pin);
  const auto it = die.electrical.find(k);
  return it == die.electrical.end() ? get(die, key, fallback) : it->second;
}

double leaky_ua(const model::DieProfile& die, std::string_view pin) {
  double ua = 0;
  for (const model::Leaky* l : die.faults.all<model::Leaky>()) {
    if (l->pin == pin) ua += l->leakage_ua;
  }
  return ua;
}

}  // namespace

ElectricalModel::ElectricalModel(model::DieProfile die) : die_(std::move(die)) {}

// k(VCC,T) = ((Vnom - Vt) / (VCC - Vt))^alpha * (1 + beta * (T - 25)), design doc §5.2.
double ElectricalModel::derate(const model::Conditions& c) const {
  const double alpha = get(die_, kDerateAlpha, 0.0);
  const double beta = get(die_, kDerateBeta, 0.0);
  double k = 1.0 + beta * (c.temp_c - kDerateRefTempC);
  if (alpha != 0.0) {
    const double vnom = get(die_, kDerateVnom, kNominalVccV);
    const double vt = get(die_, kDerateVt, 1.0);
    if (c.vcc_v <= vt) {
      throw std::domain_error("ElectricalModel::derate: VCC " + std::to_string(c.vcc_v) +
                              " V is at or below the die's Vt");
    }
    k *= std::pow((vnom - vt) / (c.vcc_v - vt), alpha);
  }
  if (!(k > 0)) throw std::domain_error("ElectricalModel::derate: non-positive derating factor");
  return k;
}

model::Logic ElectricalModel::input_level(std::string_view pin, double driven_v,
                                          const model::Conditions&) const {
  const double vt = get_pin(die_, kVinThreshold, pin, kIdealThresholdV);
  const double half_band = get_pin(die_, kVinBand, pin, 0.0) / 2;
  if (driven_v >= vt + half_band) return model::Logic::L1;
  if (driven_v < vt - half_band) return model::Logic::L0;
  return model::Logic::X;
}

// VOL = V0 + R_on * IOL; VOH = VCC - dV - R_on * |IOH|. load_ma > 0 sinks into the pin.
double ElectricalModel::output_voltage(std::string_view pin, model::Logic level, double load_ma,
                                       const model::Conditions& c) const {
  switch (level) {
    case model::Logic::L0:
      return get_pin(die_, kVolOffset, pin, 0.0) + get_pin(die_, kRonLow, pin, 0.0) * load_ma / 1000;
    case model::Logic::L1:
      return c.vcc_v - get_pin(die_, kVohDrop, pin, 0.0) +
             get_pin(die_, kRonHigh, pin, 0.0) * load_ma / 1000;
    default:
      throw std::domain_error("ElectricalModel::output_voltage: pin " + std::string(pin) +
                              " is not driving (Z/X)");
  }
}

// Symmetric leakage: current flows into the pin above VCC/2 and out of it below.
double ElectricalModel::leakage_ua(std::string_view pin, double applied_v,
                                   const model::Conditions& c) const {
  const double mag = get_pin(die_, kLeak, pin, 0.0) + leaky_ua(die_, pin);
  return applied_v >= c.vcc_v / 2 ? mag : -mag;
}

// NMOS: nearly independent of frequency (brief §6.2), plus VCC and temperature terms.
double ElectricalModel::icc_ma(const model::Conditions& c) const {
  double leak_ma = 0;
  for (const model::Leaky* l : die_.faults.all<model::Leaky>()) leak_ma += std::abs(l->leakage_ua) / 1000;
  return get(die_, kIccBase, 0.0) + get(die_, kIccPerV, 0.0) * (c.vcc_v - kNominalVccV) +
         get(die_, kIccPerC, 0.0) * (c.temp_c - kDerateRefTempC) + leak_ma;
}

int64_t ElectricalModel::load_delay_ps(std::string_view pin, double cl_pf) const {
  return std::llround(get_pin(die_, kLoadSlope, pin, 0.0) * cl_pf * 1000);
}

}  // namespace i8086::tester
