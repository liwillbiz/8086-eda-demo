#pragma once

// Fault-coverage regression (design doc §4.2, §8 step 3). Layer-private: the CLI's
// --coverage and ./regress step 3 use it. Each fault die runs the full test matrix, and the
// set of tests that do not pass at some point must equal the set `regression/expected.yaml`
// lists for that die: a listed test that passes everywhere is a coverage hole, and an
// unlisted test that fails means the fault (or a test) is not doing what we think.

#include <filesystem>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "i8086/testprog/runner.hpp"

namespace i8086::testprog {

struct Expected {
  std::map<std::string, std::set<std::string>> fault_coverage;  // die name -> test ids
};

// throws spec::SpecError on a malformed file.
Expected load_expected(const std::filesystem::path&);

struct CoverageConfig {
  RunConfig run;                       // spec, grades, VCC, temperatures, out_dir; die is ignored
  std::filesystem::path dies_dir = "spec/dies";
  std::filesystem::path expected_path = "regression/expected.yaml";
  std::vector<std::string> only;       // die names to run; empty = every die
};

struct DieCoverage {
  std::string die;
  std::string faults;                  // "StuckAt AD3=1; ..." from the die file
  bool has_die = true;                 // false: expected.yaml names a die that does not exist
  std::map<std::string, Verdict> worst;  // test -> worst verdict over the matrix
  std::set<std::string> expected, caught;
  std::set<std::string> missing() const;     // expected but never caught
  std::set<std::string> unexpected() const;  // caught but not expected
  bool ok() const { return has_die && missing().empty() && unexpected().empty(); }
};

struct CoverageResult {
  std::vector<std::string> tests;  // registry order
  std::vector<DieCoverage> dies;
  bool ok() const;
};

// Runs every dies_dir/fault_*.yaml plus every die named in expected.yaml; each run writes
// its results into out_dir/<die>/.
CoverageResult run_coverage(const CoverageConfig&, std::ostream& console);

void write_coverage_csv(std::ostream&, const CoverageResult&);
void write_coverage_md(std::ostream&, const CoverageResult&);

}  // namespace i8086::testprog
