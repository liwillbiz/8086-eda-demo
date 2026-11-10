#pragma once

// Test-matrix runner behind the i86test CLI. FROZEN INTERFACE (M0).

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "i8086/spec/spec.hpp"
#include "i8086/testprog/test_case.hpp"

namespace i8086::testprog {

struct RunConfig {
  std::filesystem::path spec_path = "spec/spec.yaml";
  std::filesystem::path die_path = "spec/dies/good.yaml";
  std::filesystem::path out_dir = "out";
  std::vector<spec::Grade> grades{spec::kAllGrades.begin(), spec::kAllGrades.end()};
  std::vector<double> vcc_v{5.0};
  std::vector<double> temp_c{25.0};
  std::string tests = "*";
};

struct ResultRow {
  std::string test;
  Point point;
  Verdict verdict = Verdict::Pass;
  std::string message;
};

struct RunSummary {
  std::vector<ResultRow> rows;
  int count(Verdict) const;
  bool ok() const { return count(Verdict::Fail) == 0 && count(Verdict::Error) == 0; }
};

// Runs every selected test at every grade x VCC x temperature point, each on a fresh
// DUT, and writes datalog.jsonl, results.csv and summary.md into out_dir.
RunSummary run(const RunConfig&, const TestRegistry& = TestRegistry::global(),
               std::ostream& console = std::cout);

}  // namespace i8086::testprog
