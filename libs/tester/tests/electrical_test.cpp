#include <gtest/gtest.h>

#include <cmath>

#include "i8086/tester/electrical_model.hpp"

namespace i8086::tester {
namespace {

using model::Conditions;
using model::Logic;

TEST(ElectricalModel, EmptyDieIsIdeal) {
  const ElectricalModel em(model::DieProfile{});
  const Conditions c{4.5, 70, 200};
  EXPECT_DOUBLE_EQ(em.derate(c), 1.0);
  EXPECT_EQ(em.input_level("READY", 2.4, c), Logic::L1);
  EXPECT_EQ(em.input_level("READY", 0.45, c), Logic::L0);
  EXPECT_DOUBLE_EQ(em.output_voltage("RD_N", Logic::L0, 2.5, c), 0.0);
  EXPECT_DOUBLE_EQ(em.output_voltage("RD_N", Logic::L1, -0.4, c), 4.5);
  EXPECT_DOUBLE_EQ(em.leakage_ua("NMI", 0.0, c), 0.0);
  EXPECT_DOUBLE_EQ(em.icc_ma(c), 0.0);
  EXPECT_EQ(em.load_delay_ps("ALE", 100), 0);
  EXPECT_THROW(em.output_voltage("AD0", Logic::Z, 0, c), std::domain_error);
}

model::DieProfile die() {
  model::DieProfile d;
  d.name = "test";
  d.electrical = {{"vin_threshold_v", 1.4},     {"vin_band_v", 0.2},
                  {"vin_threshold_v.READY", 1.8}, {"vol_offset_v", 0.1},
                  {"ron_low_ohm", 40},           {"voh_drop_v", 0.9},
                  {"ron_high_ohm", 500},         {"leak_ua", 0.5},
                  {"load_slope_ns_per_pf", 0.05}, {"load_slope_ns_per_pf.ALE", 0.1},
                  {"derate_vnom_v", 5.0},        {"derate_vt_v", 1.0},
                  {"derate_alpha", 1.3},         {"derate_beta_per_c", 0.002},
                  {"icc_base_ma", 250},          {"icc_ma_per_v", 20},
                  {"icc_ma_per_c", -0.5}};
  d.faults.faults.emplace_back(std::in_place_type<model::Leaky>, "NMI", 25.0);
  return d;
}

TEST(ElectricalModel, InputThresholdsWithPerPinOverride) {
  const ElectricalModel em(die());
  const Conditions c;
  EXPECT_EQ(em.input_level("INTR", 1.51, c), Logic::L1);
  EXPECT_EQ(em.input_level("INTR", 1.40, c), Logic::X);
  EXPECT_EQ(em.input_level("INTR", 1.29, c), Logic::L0);
  EXPECT_EQ(em.input_level("READY", 1.6, c), Logic::L0);
  EXPECT_EQ(em.input_level("READY", 1.95, c), Logic::L1);
}

TEST(ElectricalModel, OutputLevelsUnderLoad) {
  const ElectricalModel em(die());
  const Conditions c{4.5, 25, 200};
  EXPECT_NEAR(em.output_voltage("RD_N", Logic::L0, 2.5, c), 0.1 + 40 * 0.0025, 1e-12);
  EXPECT_NEAR(em.output_voltage("RD_N", Logic::L1, -0.4, c), 4.5 - 0.9 - 500 * 0.0004, 1e-12);
}

TEST(ElectricalModel, DeratingFormula) {
  const ElectricalModel em(die());
  EXPECT_DOUBLE_EQ(em.derate({5.0, 25, 200}), 1.0);
  const double k = em.derate({4.5, 70, 200});
  EXPECT_NEAR(k, std::pow(4.0 / 3.5, 1.3) * (1 + 0.002 * 45), 1e-12);
  EXPECT_LT(em.derate({5.5, 0, 200}), 1.0);
  EXPECT_THROW(em.derate({0.9, 25, 200}), std::domain_error);
}

TEST(ElectricalModel, LeakageIccAndLoad) {
  const ElectricalModel em(die());
  const Conditions c{5.0, 25, 200};
  EXPECT_DOUBLE_EQ(em.leakage_ua("INTR", 5.0, c), 0.5);
  EXPECT_DOUBLE_EQ(em.leakage_ua("INTR", 0.0, c), -0.5);
  EXPECT_DOUBLE_EQ(em.leakage_ua("NMI", 5.0, c), 25.5);  // Leaky fault
  EXPECT_NEAR(em.icc_ma({5.5, 0, 200}), 250 + 20 * 0.5 + 0.5 * 25 + 0.025, 1e-9);
  EXPECT_EQ(em.load_delay_ps("RD_N", 100), 5000);
  EXPECT_EQ(em.load_delay_ps("ALE", 100), 10000);
}

}  // namespace
}  // namespace i8086::tester
