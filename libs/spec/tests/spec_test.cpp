#include <gtest/gtest.h>

#include "i8086/common/errors.hpp"
#include "i8086/spec/spec.hpp"

namespace i8086::spec {
namespace {

const char* kSpecPath = I86_SOURCE_DIR "/spec/spec.yaml";

TEST(Spec, LoadsSkeleton) {
  const Spec s = Spec::load(kSpecPath);
  EXPECT_EQ(s.schema_version(), 1);
  EXPECT_EQ(s.device_name(), "i8086");
  EXPECT_EQ(s.pins().size(), 40u);
  EXPECT_DOUBLE_EQ(s.grade(Grade::G8086_2).fclk_max_mhz, 8.0);
  EXPECT_DOUBLE_EQ(s.grade(Grade::G8086_1).tclcl_min_ns, 100.0);
}

TEST(Spec, PinLookup) {
  const Spec s = Spec::load(kSpecPath);
  const Pin& ale = s.pin("ALE");
  EXPECT_EQ(ale.num, 25);
  EXPECT_EQ(ale.dir, Dir::Out);
  EXPECT_EQ(ale.max_mode_name, "QS0");
  EXPECT_EQ(s.pin("AD0").num, 16);
  EXPECT_THROW(s.pin("NOPE"), SpecError);
}

TEST(Spec, GradeRoundTrip) {
  for (Grade g : kAllGrades) EXPECT_EQ(parse_grade(to_string(g)), g);
  EXPECT_THROW(parse_grade("8088"), SpecError);
}

TEST(Spec, MissingFileReportsPath) {
  try {
    Spec::load("does/not/exist.yaml");
    FAIL() << "expected SpecError";
  } catch (const SpecError& e) {
    EXPECT_EQ(e.file(), "does/not/exist.yaml");
  }
}

TEST(Spec, LimitsAreStubbedUntilM1) {
  const Spec s = Spec::load(kSpecPath);
  EXPECT_THROW(s.ac("TCLAV", Grade::G8086, 200.0), NotImplemented);
}

}  // namespace
}  // namespace i8086::spec
