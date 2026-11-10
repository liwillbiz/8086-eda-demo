#pragma once

// `.pat` vector format (design doc §6.2). FROZEN INTERFACE (M0).

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace i8086::tester {

struct PatternTiming {
  int id = 0;
  // Raw attribute expressions, e.g. {"strobe", "TCLAV.max+5ns"}; resolved against
  // the spec by the tester at run time so spec changes flow into patterns.
  std::map<std::string, std::string, std::less<>> attrs;
};

struct Vector {
  int timing_id = 0;
  std::string chars;  // one char per expanded pin column: 0 1 Z | L H X M C
  uint32_t repeat = 1;
  std::string comment;
};

struct Pattern {
  std::string name;
  std::string spec_ref;           // `spec` line, informational
  std::vector<std::string> pins;  // expanded: AD[15:0] -> AD15 ... AD0
  std::vector<PatternTiming> timings;
  std::vector<Vector> vectors;

  static Pattern parse(std::string_view text, std::string_view origin = "<string>");
  static Pattern load(const std::filesystem::path&);
  std::string to_text() const;  // parse(to_text()) round-trips
};

}  // namespace i8086::tester
