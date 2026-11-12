// Fault-coverage checker (M7): expectations file, exact-set matching and the report.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "coverage.hpp"
#include "i8086/spec/spec.hpp"

namespace i8086::testprog {
namespace {

namespace fs = std::filesystem;

fs::path scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / ("i86_coverage_" + name);
  fs::remove_all(dir);
  fs::create_directories(dir / "dies");
  return dir;
}

CoverageConfig config(const fs::path& dir, const std::string& expected) {
  std::ofstream(dir / "expected.yaml") << "schema_version: 1\nwaivers: []\n" << expected;
  CoverageConfig c;
  c.run.spec_path = I86_SOURCE_DIR "/spec/spec.yaml";
  c.run.grades = {spec::Grade::G8086_1};
  c.run.vcc_v = {4.5};
  c.run.temp_c = {70};
  c.run.tests = "B4,D2";
  c.run.out_dir = dir / "out";
  c.dies_dir = dir / "dies";
  c.expected_path = dir / "expected.yaml";
  return c;
}

TEST(Coverage, LoadsExpectations) {
  const Expected e = load_expected(I86_SOURCE_DIR "/regression/expected.yaml");
  ASSERT_TRUE(e.fault_coverage.contains("fault_setup_ready"));
  EXPECT_EQ(e.fault_coverage.at("fault_setup_ready"), (std::set<std::string>{"B4"}));
  for (const auto& [die, tests] : e.fault_coverage) {
    EXPECT_TRUE(fs::exists(I86_SOURCE_DIR "/spec/dies/" + die + ".yaml")) << die;
    EXPECT_FALSE(tests.empty()) << die << " is caught by nothing";
  }
}

TEST(Coverage, MalformedExpectationsAreSpecErrors) {
  const fs::path dir = scratch("bad");
  std::ofstream(dir / "expected.yaml") << "fault_coverage: [A1]\n";
  EXPECT_THROW(load_expected(dir / "expected.yaml"), spec::SpecError);
}

TEST(Coverage, ExactMatchPassesAndAnyDifferenceFails) {
  const fs::path dir = scratch("match");
  fs::copy_file(I86_SOURCE_DIR "/spec/dies/fault_setup_ready.yaml", dir / "dies/fault_setup_ready.yaml");
  std::ostringstream console;

  const CoverageResult ok = run_coverage(config(dir, "fault_coverage:\n  fault_setup_ready: [B4]\n"), console);
  ASSERT_EQ(ok.dies.size(), 1u);
  EXPECT_TRUE(ok.ok()) << console.str();
  EXPECT_EQ(ok.dies[0].caught, (std::set<std::string>{"B4"}));
  EXPECT_EQ(ok.dies[0].faults, "SetupDegrade READY +45 ns");

  const CoverageResult wrong = run_coverage(config(dir, "fault_coverage:\n  fault_setup_ready: [D2]\n"), console);
  EXPECT_FALSE(wrong.ok());
  EXPECT_EQ(wrong.dies[0].missing(), (std::set<std::string>{"D2"}));
  EXPECT_EQ(wrong.dies[0].unexpected(), (std::set<std::string>{"B4"}));

  const CoverageResult unlisted = run_coverage(config(dir, "fault_coverage: {}\n"), console);
  EXPECT_FALSE(unlisted.ok()) << "a fault die without expectations must not pass silently";

  std::ostringstream md, csv;
  write_coverage_md(md, wrong);
  write_coverage_csv(csv, wrong);
  EXPECT_NE(md.str().find("0 of 1 fault dies"), std::string::npos) << md.str();
  EXPECT_NE(md.str().find("missed D2; unexpected B4"), std::string::npos) << md.str();
  EXPECT_NE(csv.str().find("fault_setup_ready,B4,0,1,fail"), std::string::npos) << csv.str();
}

TEST(Coverage, ExpectationWithoutADieFails) {
  const fs::path dir = scratch("nodie");
  std::ostringstream console;
  const CoverageResult r = run_coverage(config(dir, "fault_coverage:\n  fault_gone: [A1]\n"), console);
  ASSERT_EQ(r.dies.size(), 1u);
  EXPECT_FALSE(r.dies[0].has_die);
  EXPECT_FALSE(r.ok());
}

}  // namespace
}  // namespace i8086::testprog
