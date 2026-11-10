// Summary report and results.csv: matrix, failure messages from the datalog, worst-margin
// table, and the unverified-limit banner.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "i8086/testprog/runner.hpp"

namespace i8086::testprog {
namespace {

class Measures final : public TestCase {
 public:
  std::string_view id() const override { return "Z1"; }
  std::string_view title() const override { return "parametric, unverified limit"; }
  Verdict run(TestContext& ctx) override {
    const spec::Limit lim{10.0, 50.0, "ns", "DS §A.C.", false};
    ctx.log.record("TCLAV", ctx.vcc_v * 8, lim, Verdict::Pass);  // margin shrinks with VCC
    return Verdict::Pass;
  }
};

class Fails final : public TestCase {
 public:
  std::string_view id() const override { return "Z2"; }
  std::string_view title() const override { return "functional fail"; }
  Verdict run(TestContext& ctx) override {
    ctx.log.record("ok", 1.0, spec::Limit{1.0, 1.0, "", "brief", true}, Verdict::Pass);
    ctx.log.record("AL", 0.0, spec::Limit{128.0, 128.0, "", "brief §7 D1", true}, Verdict::Fail,
                   "expected 0080h, got 0000h");
    return Verdict::Fail;
  }
};

std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

TEST(Report, SummaryAndCsv) {
  TestRegistry reg;
  reg.add([] { return std::make_unique<Measures>(); });
  reg.add([] { return std::make_unique<Fails>(); });
  RunConfig cfg;
  cfg.spec_path = I86_SOURCE_DIR "/spec/spec.yaml";
  cfg.die_path = I86_SOURCE_DIR "/spec/dies/good.yaml";
  cfg.out_dir = std::filesystem::temp_directory_path() / "i86_report_out";
  cfg.grades = {spec::Grade::G8086};
  cfg.vcc_v = {4.5, 5.5};
  cfg.temp_c = {25};
  std::ostringstream console;
  const RunSummary s = run(cfg, reg, console);

  ASSERT_EQ(s.rows.size(), 4u);
  EXPECT_EQ(s.rows[1].message, "AL = 0, expected 128: expected 0080h, got 0000h");

  const std::string md = slurp(cfg.out_dir / "summary.md");
  EXPECT_NE(md.find("## Results matrix"), std::string::npos) << md;
  EXPECT_NE(md.find("| test | 4.5 V / 25 °C | 5.5 V / 25 °C |"), std::string::npos) << md;
  EXPECT_NE(md.find("| Z2 | **FAIL** | **FAIL** |"), std::string::npos) << md;
  EXPECT_NE(md.find("## Failures and errors"), std::string::npos) << md;
  // Worst TCLAV margin is at 5.5 V: 50 - 44 = 6 ns.
  EXPECT_NE(md.find("| Z1 | TCLAV | 44 ns | ≥ 10, ≤ 50 | 6 | 8086 5.5 V / 25 °C | 2 |"), std::string::npos) << md;
  EXPECT_NE(md.find("**Unverified limits:**"), std::string::npos) << md;
  EXPECT_NE(md.find("Z1 `TCLAV` (DS §A.C.)"), std::string::npos) << md;
  EXPECT_EQ(md.find("Z2 `AL`"), std::string::npos) << md;

  const std::string csv = slurp(cfg.out_dir / "results.csv");
  EXPECT_NE(csv.find("Z2,good,8086,4.5,25,fail,\"AL = 0, expected 128: expected 0080h, got 0000h\""),
            std::string::npos)
      << csv;
}

}  // namespace
}  // namespace i8086::testprog
