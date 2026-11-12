#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "i8086/tester/pattern.hpp"
#include "timing_expr.hpp"

namespace i8086::tester {
namespace {

constexpr const char* kResetPat = R"(# reset_vector.pat — A1
pattern reset_vector
spec    spec/spec.yaml
pins    RESET READY INTR NMI HOLD MN_MX_N  ALE RD_N WR_N M_IO_N  AD[15:0]           A[19:16]
timing  0 period=TCLCL drive=0ns  strobe=TCLAV.max+5ns grade=8086-2   # symbolic refs into spec
#       ---- drive ----------- | ---- compare ----------------------------------
v 0     1     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 4
v 0     0     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 7
v 0     0     1     0    0   0  1      H    H    H    H     HHHHHHHHHHHHLLLL    HHHH   # T1: addr FFFF0h
)";

TEST(Pattern, ParsesDesignDocExample) {
  const Pattern p = Pattern::parse(kResetPat, "reset_vector.pat");
  EXPECT_EQ(p.name, "reset_vector");
  EXPECT_EQ(p.spec_ref, "spec/spec.yaml");
  ASSERT_EQ(p.pins.size(), 10u + 16u + 4u);
  EXPECT_EQ(p.pins[10], "AD15");
  EXPECT_EQ(p.pins[25], "AD0");
  EXPECT_EQ(p.pins[26], "A19");
  EXPECT_EQ(p.pins[29], "A16");
  ASSERT_EQ(p.timings.size(), 1u);
  EXPECT_EQ(p.timings[0].attrs.at("strobe"), "TCLAV.max+5ns");
  EXPECT_EQ(p.timings[0].attrs.at("grade"), "8086-2");
  ASSERT_EQ(p.vectors.size(), 3u);
  EXPECT_EQ(p.vectors[0].repeat, 4u);
  EXPECT_EQ(p.vectors[1].repeat, 7u);
  EXPECT_EQ(p.vectors[2].repeat, 1u);
  EXPECT_EQ(p.vectors[2].comment, "T1: addr FFFF0h");
  EXPECT_EQ(p.vectors[2].chars.substr(6), "HHHHHHHHHHHHHHHHLLLLHHHH");
}

TEST(Pattern, RoundTrips) {
  const Pattern p = Pattern::parse(kResetPat);
  const std::string text = p.to_text();
  const Pattern q = Pattern::parse(text);
  EXPECT_EQ(q.to_text(), text);
  EXPECT_EQ(q.pins, p.pins);
  ASSERT_EQ(q.vectors.size(), p.vectors.size());
  for (std::size_t i = 0; i < p.vectors.size(); ++i) {
    EXPECT_EQ(q.vectors[i].chars, p.vectors[i].chars);
    EXPECT_EQ(q.vectors[i].repeat, p.vectors[i].repeat);
    EXPECT_EQ(q.vectors[i].comment, p.vectors[i].comment);
    EXPECT_EQ(q.vectors[i].timing_id, p.vectors[i].timing_id);
  }
  EXPECT_EQ(q.timings[0].attrs, p.timings[0].attrs);
  EXPECT_NE(text.find("AD[15:0]"), std::string::npos) << text;
  EXPECT_NE(text.find("A[19:16]"), std::string::npos) << text;
}

TEST(Pattern, AscendingBusAndPerPinAttrs) {
  const Pattern p = Pattern::parse(
      "pattern t\npins X[0:2] RESET\ntiming 1 drive.RESET=10ns fmt.RESET=RZ\nv 1 LHX 1\n");
  EXPECT_EQ(p.pins, (std::vector<std::string>{"X0", "X1", "X2", "RESET"}));
  EXPECT_EQ(Pattern::parse(p.to_text()).pins, p.pins);
}

TEST(Pattern, LoadsFromFile) {
  const auto path = std::filesystem::temp_directory_path() / "i86_reset_vector.pat";
  std::ofstream(path) << kResetPat;
  EXPECT_EQ(Pattern::load(path).vectors.size(), 3u);
  EXPECT_THROW(Pattern::load(path.string() + ".missing"), std::runtime_error);
}

struct BadCase {
  const char* text;
  const char* message;
};

class PatternErrors : public ::testing::TestWithParam<BadCase> {};

TEST_P(PatternErrors, ReportOriginAndLine) {
  try {
    Pattern::parse(GetParam().text, "bad.pat");
    FAIL() << "expected a parse error";
  } catch (const std::runtime_error& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find(GetParam().message), std::string::npos) << what;
    EXPECT_EQ(what.rfind("bad.pat:", 0), 0u) << what;
  }
}

INSTANTIATE_TEST_SUITE_P(
    All, PatternErrors,
    ::testing::Values(
        BadCase{"pattern p\npins A B\nv 0 0\n", "bad.pat:3: vector has 1 pin chars"},
        BadCase{"pattern p\npins A\nv 0 q\n", "bad.pat:3: bad vector char 'q'"},
        BadCase{"pattern p\npins A\nv 0 1 repeat 0\n", "repeat N"},
        BadCase{"pattern p\npins A A\n", "duplicate pin 'A'"},
        BadCase{"pattern p\npins A\ntiming 0 bogus=1\n", "unknown timing attribute 'bogus'"},
        BadCase{"pattern p\npins A\ntiming 0 drive.B=1\n", "bad.pat:3: timing attribute names pin 'B'"},
        BadCase{"pattern p\npins A\ntiming 0\ntiming 0\n", "duplicate timing 0"},
        BadCase{"pins A\n", "missing 'pattern NAME'"},
        BadCase{"pattern p\nv 0 1\n", "vector before 'pins'"},
        BadCase{"pattern p\npins AD[15:x]\n", "bad bus range"},
        BadCase{"pattern p\npins A\nfoo\n", "unknown keyword 'foo'"}));

TEST(TimingExpr, UnitsSymbolsAndErrors) {
  using detail::eval_timing_expr;
  detail::TimingExprEnv env;
  env.tclcl_ns = 125;
  env.symbol = [](const detail::SymbolRef& s) { return s.max ? 60.0 : 10.0; };
  EXPECT_DOUBLE_EQ(eval_timing_expr("TCLAV.max+5ns", env), 65.0);
  EXPECT_DOUBLE_EQ(eval_timing_expr("TCLCL", env), 125.0);
  EXPECT_DOUBLE_EQ(eval_timing_expr("2*(TCLCL-TCLAV.min)/4", env), 57.5);
  EXPECT_DOUBLE_EQ(eval_timing_expr("1500ps + 0.5us", env), 501.5);
  EXPECT_DOUBLE_EQ(eval_timing_expr("-3", env), -3.0);
  EXPECT_THROW(eval_timing_expr("TCLAV", env), detail::TimingExprError);
  EXPECT_THROW(eval_timing_expr("5 fs", env), detail::TimingExprError);
  EXPECT_THROW(eval_timing_expr("1/0", env), detail::TimingExprError);
  EXPECT_THROW(eval_timing_expr("(1", env), detail::TimingExprError);
  EXPECT_THROW(eval_timing_expr("TCLAV.typ", env), detail::TimingExprError);
  EXPECT_THROW(eval_timing_expr("TCLCL", detail::TimingExprEnv{}), detail::TimingExprError);
}

}  // namespace
}  // namespace i8086::tester
