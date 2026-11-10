#pragma once

// Datalog (design doc §6.3). FROZEN INTERFACE (M0).

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "i8086/spec/spec.hpp"

namespace i8086::testprog {

enum class Verdict { Pass, Fail, Error, Skip };
std::string_view to_string(Verdict);

// One point of the test matrix; set by the runner before each test.
struct Point {
  std::string die;
  spec::Grade grade = spec::Grade::G8086;
  double vcc_v = 5.0;
  double temp_c = 25.0;
};

struct Record {
  std::string run, test;
  Point point;
  std::string param;
  std::optional<double> value;
  spec::Limit limit;
  Verdict result = Verdict::Pass;
  std::optional<double> margin;  // distance to the nearest limit; negative = outside
  std::string note;
};

class Datalog {
 public:
  explicit Datalog(std::string run_id);

  void begin(std::string test, const Point&);
  void record(std::string param, std::optional<double> value, const spec::Limit&, Verdict,
              std::string note = {});

  const std::string& run_id() const { return run_id_; }
  const std::vector<Record>& records() const { return records_; }
  void write_jsonl(std::ostream&) const;  // one JSON object per line

 private:
  std::string run_id_;
  std::string test_;
  Point point_;
  std::vector<Record> records_;
};

}  // namespace i8086::testprog
