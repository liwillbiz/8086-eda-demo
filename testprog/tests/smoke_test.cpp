// End-to-end smoke test: spec -> DeviceModel(+FaultInjector) -> SimTester -> TestCase
// -> runner -> datalog/summary, all through the frozen interfaces.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "i8086/testprog/runner.hpp"

namespace i8086::testprog {
namespace {

class LevelsCase final : public TestCase {
 public:
  std::string_view id() const override { return "Z1"; }
  std::string_view title() const override { return "smoke: program levels and log"; }
  Verdict run(TestContext& ctx) override {
    tester::LevelSet levels;
    levels.vcc = ctx.vcc_v;
    ctx.t.set_levels(levels);
    ctx.t.power(true);
    spec::Limit vcc_limit{4.5, 5.5, "V", "DS §D.C.", true};
    ctx.log.record("VCC", ctx.vcc_v, vcc_limit, Verdict::Pass);
    return Verdict::Pass;
  }
};

class StubbedCase final : public TestCase {
 public:
  std::string_view id() const override { return "Z2"; }
  std::string_view title() const override { return "smoke: stub reports Error"; }
  Verdict run(TestContext& ctx) override {
    ctx.t.run_program(tester::MemoryImage{}, tester::RunOpts{});
    return Verdict::Pass;
  }
};

TestRegistry smoke_registry() {
  TestRegistry reg;
  reg.add([] { return std::make_unique<LevelsCase>(); });
  reg.add([] { return std::make_unique<StubbedCase>(); });
  return reg;
}

std::filesystem::path temp_out() {
  auto dir = std::filesystem::temp_directory_path() / "i86_smoke_out";
  std::filesystem::remove_all(dir);
  return dir;
}

TEST(Smoke, FullStackRunsThroughFrozenInterfaces) {
  RunConfig cfg;
  cfg.spec_path = I86_SOURCE_DIR "/spec/spec.yaml";
  cfg.die_path = I86_SOURCE_DIR "/spec/dies/good.yaml";
  cfg.out_dir = temp_out();
  cfg.vcc_v = {4.5, 5.5};

  std::ostringstream console;
  const RunSummary s = run(cfg, smoke_registry(), console);

  // 2 tests x 3 grades x 2 VCC x 1 temp.
  ASSERT_EQ(s.rows.size(), 12u);
  EXPECT_EQ(s.count(Verdict::Pass), 6);
  EXPECT_EQ(s.count(Verdict::Error), 6);  // NotImplemented is Error, never Fail
  EXPECT_EQ(s.count(Verdict::Fail), 0);
  EXPECT_FALSE(s.ok());

  for (const char* f : {"datalog.jsonl", "results.csv", "summary.md"}) {
    EXPECT_TRUE(std::filesystem::exists(cfg.out_dir / f)) << f;
  }
  std::ifstream datalog(cfg.out_dir / "datalog.jsonl");
  std::string first;
  std::getline(datalog, first);
  EXPECT_NE(first.find("\"test\":\"Z1\""), std::string::npos) << first;
  EXPECT_NE(first.find("\"margin\":0"), std::string::npos) << first;
}

TEST(Smoke, EmptyRegistryIsGreen) {
  RunConfig cfg;
  cfg.spec_path = I86_SOURCE_DIR "/spec/spec.yaml";
  cfg.die_path = I86_SOURCE_DIR "/spec/dies/good.yaml";
  cfg.out_dir = temp_out();
  std::ostringstream console;
  EXPECT_TRUE(run(cfg, TestRegistry{}, console).ok());
}

TEST(Registry, SelectorsFollowBriefOrder) {
  TestRegistry reg;
  struct Named final : TestCase {
    explicit Named(std::string i) : id_(std::move(i)) {}
    std::string_view id() const override { return id_; }
    std::string_view title() const override { return id_; }
    Verdict run(TestContext&) override { return Verdict::Pass; }
    std::string id_;
  };
  for (const char* id : {"D10", "A1", "D2", "B3", "D11", "A2"}) {
    reg.add([id] { return std::make_unique<Named>(id); });
  }
  EXPECT_EQ(reg.ids(), (std::vector<std::string>{"A1", "A2", "B3", "D2", "D10", "D11"}));
  EXPECT_EQ(reg.select("A*,D2-D10"), (std::vector<std::string>{"A1", "A2", "D2", "D10"}));
  EXPECT_THROW(reg.select("C9"), std::invalid_argument);
  EXPECT_THROW(reg.add([] { return std::make_unique<Named>("A1"); }), std::logic_error);
}

}  // namespace
}  // namespace i8086::testprog
