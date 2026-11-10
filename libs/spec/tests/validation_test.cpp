// Schema validation: each case mutates the real spec.yaml in one place and expects a
// SpecError that names the file and the offending line.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "i8086/spec/spec.hpp"

namespace i8086::spec {
namespace {

const char* kSpecPath = I86_SOURCE_DIR "/spec/spec.yaml";

std::string read_spec() {
  std::ifstream in(kSpecPath);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

int line_of(const std::string& text, const std::string& needle) {
  const auto pos = text.find(needle);
  if (pos == std::string::npos) return -1;
  return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<long>(pos), '\n'));
}

struct Case {
  const char* name;
  const char* from;      // replaced once in spec.yaml
  const char* to;
  const char* message;   // expected substring of what()
  const char* at_line;   // substring of the mutated text whose line the error must point to
};

const Case kCases[] = {
    {"UnknownTopLevelKey", "\ndc:\n", "\ndcc:\n", "unknown key 'dcc'", "dcc:"},
    {"UnknownParamKey", "    desc: \"CLK low time\"", "    descr: \"CLK low time\"",
     "unknown key 'descr'", "descr:"},
    {"MissingRef", "TCLDX: { desc: \"Data-in hold after CLK\", unit: ns, min: 10, ref: \"DS §A.C. Characteristics\", ",
     "TCLDX: { desc: \"Data-in hold after CLK\", unit: ns, min: 10, ",
     "missing required key 'ref'", "TCLDX:"},
    {"VerifiedNotBool", "TCLDX: { desc: \"Data-in hold after CLK\", unit: ns, min: 10, ref: \"DS §A.C. Characteristics\", verified: true",
     "TCLDX: { desc: \"Data-in hold after CLK\", unit: ns, min: 10, ref: \"DS §A.C. Characteristics\", verified: maybe",
     "expected true or false", "TCLDX:"},
    {"UnknownUnit", "TOLOH: { desc: \"Output rise time\", unit: ns", "TOLOH: { desc: \"Output rise time\", unit: nsec",
     "unknown unit 'nsec'", "TOLOH:"},
    {"BadExpression", "{ min: \"2*TCLCL-75\" }", "{ min: \"2*TCLCL-\" }",
     "bad expression '2*TCLCL-'", "2*TCLCL-\""},
    {"UnknownVariable", "{ min: \"TCLCL-45\" }", "{ min: \"TCLK-45\" }",
     "'TCLK' is not allowed", "TCLK-45"},
    {"UnknownReference", "{ min: \"TCLCH.min-20\" }", "{ min: \"TCLHC.min-20\" }",
     "unknown AC parameter 'TCLHC'", "  TLHLL:"},
    {"ReferenceToMissingBound", "{ min: \"TCLCH.min-20\" }", "{ min: \"TCLCH.max-20\" }",
     "has no max", "  TLHLL:"},
    {"ReferenceCycle", "\"8086\":   { min: \"2/3*TCLCL-15\" }\n    \"8086-2\": { min: \"2/3*TCLCL-15\" }\n    \"8086-1\": { min: 53 }",
     "\"8086\":   { min: \"TLHLL.min+20\" }\n    \"8086-2\": { min: \"2/3*TCLCL-15\" }\n    \"8086-1\": { min: 53 }",
     "reference cycle", "  TCLCH:"},
    {"MinAboveMax", "\"8086\":   { min: 10, max: 110 }", "\"8086\":   { min: 120, max: 110 }",
     "min > max for grade 8086", "  TCLAV:"},
    {"MissingGrade", "    \"8086-1\": { min: \"2*TCLCL-35\" }\n", "", "missing grade 8086-1", "  TWLWH:"},
    {"FlatAndPerGrade", "    desc: \"WR# width\"", "    min: 1\n    desc: \"WR# width\"",
     "use either min/max or per-grade blocks", "  TWLWH:"},
    {"TclclIsReserved", "  TOLOH:", "  TCLCL: { unit: ns, min: 1, ref: x, verified: true }\n  TOLOH:",
     "TCLCL is defined by `grades`", "  TCLCL:"},
    {"DcMayNotUseTclcl", "max: \"VCC+0.5\"", "max: \"TCLCL+0.5\"", "DC bounds may use VCC",
     "  VIH: {"},
    {"FlatSectionNeedsNumbers", "PD:      { desc: \"Power dissipation\",              unit: W, max: 2.5",
     "PD:      { desc: \"Power dissipation\",              unit: W, max: \"VCC/2\"",
     "expected a number", "PD:"},
    {"VccConditionRequired", "  VCC:       {", "  VDD:       {", "conditions.VCC needs min and max",
     "  VDD:"},
    {"DecodeMissingEntry", ", \"111\": passive }", " }", "decode.status is missing entries",
     "table: { \"000\": int_ack"},
    {"DecodeBadKey", "\"10\": empty", "\"12\": empty", "must be 2 binary digits", "\"12\": empty"},
    {"DuplicatePinNumber", "num: 40, name: VCC", "num: 39, name: VCC", "duplicate pin number",
     "num: 39, name: VCC"},
    {"DuplicatePinName", "name: GND2,", "name: GND,", "duplicate pin name 'GND'",
     "num: 20, name: GND,"},
    {"GradeFclkMismatch", "fclk_max_mhz: 8,", "fclk_max_mhz: 9,", "fclk_max_mhz does not match",
     "\"8086-2\": { fclk_max_mhz: 9"},
};

class Validation : public testing::TestWithParam<Case> {};

TEST_P(Validation, RejectsWithFileAndLine) {
  const Case& c = GetParam();
  std::string text = read_spec();
  const auto pos = text.find(c.from);
  ASSERT_NE(pos, std::string::npos) << "fixture text not found in spec.yaml: " << c.from;
  text.replace(pos, std::string(c.from).size(), c.to);
  const int expected_line = line_of(text, c.at_line);
  ASSERT_GT(expected_line, 0) << "line marker not found: " << c.at_line;

  const std::string path = testing::TempDir() + "spec_" + c.name + ".yaml";
  std::ofstream(path) << text;
  try {
    Spec::load(path);
    FAIL() << "expected SpecError";
  } catch (const SpecError& e) {
    EXPECT_NE(std::string(e.what()).find(c.message), std::string::npos) << e.what();
    EXPECT_EQ(e.file(), path);
    EXPECT_EQ(e.line(), expected_line) << e.what();
  }
}

INSTANTIATE_TEST_SUITE_P(SpecYaml, Validation, testing::ValuesIn(kCases),
                         [](const auto& p) { return std::string(p.param.name); });

TEST(Validation, CheckedInSpecIsValid) { EXPECT_NO_THROW(Spec::load(kSpecPath)); }

}  // namespace
}  // namespace i8086::spec
