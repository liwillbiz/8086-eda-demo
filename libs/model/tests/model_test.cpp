#include <gtest/gtest.h>

#include "i8086/model/device_model.hpp"
#include "i8086/model/fault_injector.hpp"

namespace i8086::model {
namespace {

TEST(DieProfile, LoadsGoodDie) {
  const DieProfile die = DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");
  EXPECT_EQ(die.name, "good");
  EXPECT_TRUE(die.faults.empty());
}

TEST(FaultSet, FindsLogicBugs) {
  FaultSet fs{{LogicBug{"inc_clears_cf"}, StuckAt{"AD3", 0, std::nullopt}}};
  EXPECT_TRUE(fs.has_logic_bug("inc_clears_cf"));
  EXPECT_FALSE(fs.has_logic_bug("shift_count_masked"));
  EXPECT_EQ(fs.all<StuckAt>().size(), 1u);
}

TEST(Behavioral, StubIdlesInTi) {
  const auto spec = spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml");
  auto dut = make_behavioral(spec, DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml"), {});
  dut->set_conditions({5.0, 25.0, 200.0});
  dut->clock_edge(Edge::Rising);
  dut->clock_edge(Edge::Falling);
  EXPECT_EQ(dut->bus_state().t, TState::Ti);
  EXPECT_EQ(dut->bus_state().clk, 1u);
  EXPECT_NE(dynamic_cast<FaultInjector*>(dut.get()), nullptr);
}

}  // namespace
}  // namespace i8086::model
