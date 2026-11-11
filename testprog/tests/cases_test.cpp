// The M5 test cases (A1-A3, D1-D6, D8, D9, D11) and the M8 interrupt, HOLD and HLT cases
// (A4-A7, D7, D10): registered, green on the good die, and the M5 ones able to catch the
// behavioral faults the design doc assigns to them (§4.2).

#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>

#include "i8086/model/device_model.hpp"
#include "i8086/tester/sim_tester.hpp"
#include "i8086/testprog/runner.hpp"

namespace i8086::testprog {
namespace {

const std::vector<std::string> kM5 = {"A1", "A2", "A3", "D1", "D2", "D3", "D4", "D5", "D6", "D8", "D9", "D11"};
const std::vector<std::string> kM8 = {"A4", "A5", "A6", "A7", "D7", "D10"};

struct Fixture {
  spec::Spec spec = spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml");
  model::DieProfile die = model::DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");
  golden::Emulator golden;
};

// Runs one registered case on a die with `faults`, at the 8086-1 corner (fastest clock).
Verdict run_case(Fixture& f, const std::string& id, model::FaultSet faults, Datalog& log) {
  tester::SimTester t(f.spec, model::make_behavioral(f.spec, f.die, std::move(faults)),
                      tester::ElectricalModel(f.die));
  tester::LevelSet levels;
  levels.vcc = 4.5;
  t.set_levels(levels);
  t.set_temperature(70);
  t.power(true);
  log.begin(id, {f.die.name, spec::Grade::G8086_1, 4.5, 70});
  TestContext ctx{t, f.spec, spec::Grade::G8086_1, 4.5, 70, log, &f.golden};
  return TestRegistry::global().create(id)->run(ctx);
}

model::FaultSet logic_bug(std::string name) {
  model::FaultSet fs;
  fs.faults.emplace_back(model::LogicBug{std::move(name)});
  return fs;
}

TEST(Cases, M5SetIsRegistered) {
  const auto ids = TestRegistry::global().ids();
  for (const auto& id : kM5) EXPECT_NE(std::ranges::find(ids, id), ids.end()) << id;
}

TEST(Cases, M8SetIsRegistered) {
  const auto ids = TestRegistry::global().ids();
  for (const auto& id : kM8) EXPECT_NE(std::ranges::find(ids, id), ids.end()) << id;
}

TEST(Cases, AllPassOnGoodDieAcrossTheMatrix) {
  RunConfig cfg;
  cfg.spec_path = I86_SOURCE_DIR "/spec/spec.yaml";
  cfg.die_path = I86_SOURCE_DIR "/spec/dies/good.yaml";
  cfg.out_dir = std::filesystem::temp_directory_path() / "i86_cases_out";
  cfg.vcc_v = {4.5, 5.5};
  cfg.temp_c = {0, 70};
  cfg.tests = "A*,D*";
  std::ostringstream console;
  const RunSummary s = run(cfg, TestRegistry::global(), console);
  EXPECT_EQ(s.rows.size(), (kM5.size() + kM8.size()) * 3 * 2 * 2);
  EXPECT_TRUE(s.ok()) << console.str();
  EXPECT_EQ(s.count(Verdict::Pass), static_cast<int>(s.rows.size())) << console.str();
}

struct FaultCase {
  const char* bug;
  const char* catches;
};

class CatchesLogicBug : public testing::TestWithParam<FaultCase> {};

TEST_P(CatchesLogicBug, OnlyTheExpectedTestFails) {
  Fixture f;
  for (const auto& id : kM5) {
    Datalog log("t");
    const Verdict v = run_case(f, id, logic_bug(GetParam().bug), log);
    const bool expect_fail = id == GetParam().catches;
    EXPECT_EQ(v, expect_fail ? Verdict::Fail : Verdict::Pass) << id << " with " << GetParam().bug;
    if (expect_fail) {
      // Caught twice: by the golden diff and by the brief's own expected value.
      int failed = 0;
      for (const auto& r : log.records()) failed += r.result == Verdict::Fail;
      EXPECT_GE(failed, 2) << id;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Model, CatchesLogicBug,
                         testing::Values(FaultCase{"inc_clears_cf", "D2"}, FaultCase{"shift_count_masked", "D8"}),
                         [](const testing::TestParamInfo<FaultCase>& p) { return std::string(p.param.bug); });

}  // namespace
}  // namespace i8086::testprog
