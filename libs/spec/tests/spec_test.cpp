#include <gtest/gtest.h>

#include <set>

#include "i8086/spec/spec.hpp"

namespace i8086::spec {
namespace {

const char* kSpecPath = I86_SOURCE_DIR "/spec/spec.yaml";

const Spec& spec() {
  static const Spec s = Spec::load(kSpecPath);
  return s;
}

TEST(Spec, LoadsDeviceAndGrades) {
  const Spec& s = spec();
  EXPECT_EQ(s.schema_version(), 1);
  EXPECT_EQ(s.device_name(), "i8086");
  EXPECT_EQ(s.pins().size(), 40u);
  EXPECT_DOUBLE_EQ(s.grade(Grade::G8086_2).fclk_max_mhz, 8.0);
  EXPECT_DOUBLE_EQ(s.grade(Grade::G8086_1).tclcl_min_ns, 100.0);
}

TEST(Spec, PinLookup) {
  const Spec& s = spec();
  const Pin& ale = s.pin("ALE");
  EXPECT_EQ(ale.num, 25);
  EXPECT_EQ(ale.dir, Dir::Out);
  EXPECT_EQ(ale.max_mode_name, "QS0");
  EXPECT_EQ(s.pin("AD0").num, 16);
  EXPECT_EQ(s.pin("AD0").bus, "AD");
  EXPECT_TRUE(s.pin("RD_N").active_low);
  EXPECT_TRUE(s.pin("RD_N").tristate);
  EXPECT_THROW(s.pin("NOPE"), SpecError);
}

TEST(Spec, PinsCoverDip40) {
  std::set<int> nums;
  for (const Pin& p : spec().pins()) nums.insert(p.num);
  EXPECT_EQ(nums.size(), 40u);
  EXPECT_EQ(*nums.begin(), 1);
  EXPECT_EQ(*nums.rbegin(), 40);
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

// Golden table: brief §4.2 evaluated at each grade's minimum TCLCL. The brief prints the
// formula results rounded to whole ns, hence the 0.5 ns tolerance.
struct AcRow {
  const char* sym;
  Grade grade;
  std::optional<double> min, max;
};

const AcRow kAcGolden[] = {
    {"TCLCL", Grade::G8086, 200, 500},     {"TCLCL", Grade::G8086_2, 125, 500},
    {"TCLCL", Grade::G8086_1, 100, 500},   {"TCLCH", Grade::G8086, 118, {}},
    {"TCLCH", Grade::G8086_2, 68, {}},     {"TCLCH", Grade::G8086_1, 53, {}},
    {"TCHCL", Grade::G8086, 69, {}},       {"TCHCL", Grade::G8086_2, 44, {}},
    {"TCHCL", Grade::G8086_1, 39, {}},     {"TCH1CH2", Grade::G8086, {}, 10},
    {"TCL2CL1", Grade::G8086_1, {}, 10},   {"TDVCL", Grade::G8086, 30, {}},
    {"TDVCL", Grade::G8086_2, 20, {}},     {"TDVCL", Grade::G8086_1, 5, {}},
    {"TCLDX", Grade::G8086_2, 10, {}},     {"TRYHCH", Grade::G8086, 118.33, {}},
    {"TRYHCH", Grade::G8086_1, 51.67, {}}, {"TCHRYX", Grade::G8086, 30, {}},
    {"TCHRYX", Grade::G8086_2, 20, {}},    {"TINVCH", Grade::G8086, 30, {}},
    {"TINVCH", Grade::G8086_1, 15, {}},    {"TCLAV", Grade::G8086, 10, 110},
    {"TCLAV", Grade::G8086_2, 10, 60},     {"TCLAV", Grade::G8086_1, 10, 50},
    {"TLHLL", Grade::G8086, 98.33, {}},    {"TLHLL", Grade::G8086_2, 58.33, {}},
    {"TLHLL", Grade::G8086_1, 43, {}},     {"TCLRL", Grade::G8086, 10, 165},
    {"TCLRL", Grade::G8086_2, 10, 100},    {"TCLRH", Grade::G8086, 10, 150},
    {"TCLRH", Grade::G8086_2, 10, 80},     {"TRLRH", Grade::G8086, 325, {}},
    {"TRLRH", Grade::G8086_2, 200, {}},    {"TRLRH", Grade::G8086_1, 160, {}},
    {"TWLWH", Grade::G8086, 340, {}},      {"TWLWH", Grade::G8086_2, 210, {}},
    {"TWLWH", Grade::G8086_1, 165, {}},    {"TRHAV", Grade::G8086, 155, {}},
    {"TRHAV", Grade::G8086_2, 85, {}},     {"TRHAV", Grade::G8086_1, 65, {}},
    {"TOLOH", Grade::G8086, {}, 20},       {"TOHOL", Grade::G8086_2, {}, 12},
};

TEST(SpecAc, GoldenTableAtGradeMinimumPeriod) {
  const Spec& s = spec();
  for (const AcRow& row : kAcGolden) {
    SCOPED_TRACE(std::string(row.sym) + " @ " + std::string(to_string(row.grade)));
    const Limit lim = s.ac(row.sym, row.grade, s.grade(row.grade).tclcl_min_ns);
    EXPECT_EQ(lim.unit, "ns");
    ASSERT_EQ(lim.min.has_value(), row.min.has_value());
    ASSERT_EQ(lim.max.has_value(), row.max.has_value());
    if (row.min) {
      EXPECT_NEAR(*lim.min, *row.min, 0.5);
    }
    if (row.max) {
      EXPECT_NEAR(*lim.max, *row.max, 0.5);
    }
  }
}

TEST(SpecAc, ExpressionsFollowTheTestPeriod) {
  const Spec& s = spec();
  // Design doc §12: "limit 2*TCLCL-40 at TCLCL = 100 ns" -> 160 ns.
  EXPECT_DOUBLE_EQ(*s.ac("TRLRH", Grade::G8086_1, 100).min, 160);
  EXPECT_DOUBLE_EQ(*s.ac("TRLRH", Grade::G8086_1, 250).min, 460);
  EXPECT_DOUBLE_EQ(*s.ac("TRHAV", Grade::G8086, 500).min, 455);
  EXPECT_NEAR(*s.ac("TCLCH", Grade::G8086_2, 300).min, 185, 1e-9);
  // Fixed values do not move with TCLCL.
  EXPECT_DOUBLE_EQ(*s.ac("TCLCH", Grade::G8086_1, 300).min, 53);
  EXPECT_DOUBLE_EQ(*s.ac("TCLAV", Grade::G8086, 400).max, 110);
  // TLHLL = TCLCH.min - 20, evaluated at the same period.
  EXPECT_NEAR(*s.ac("TLHLL", Grade::G8086, 300).min, 2.0 / 3 * 300 - 15 - 20, 1e-9);
}

TEST(SpecAc, OutOfGradePeriodsStillEvaluate) {
  // The shmoo runs below the grade minimum; ac() must not refuse those periods.
  EXPECT_DOUBLE_EQ(*spec().ac("TRLRH", Grade::G8086_1, 80).min, 120);
}

TEST(SpecAc, Provenance) {
  const Spec& s = spec();
  const Limit tclav = s.ac("TCLAV", Grade::G8086, 200);
  EXPECT_EQ(tclav.ref, "DS §A.C. Characteristics");
  EXPECT_TRUE(tclav.verified);
  EXPECT_FALSE(s.ac("TCLAV", Grade::G8086_1, 100).verified);
  EXPECT_FALSE(s.ac("TDVCL", Grade::G8086_1, 100).verified);
  EXPECT_TRUE(s.ac("TDVCL", Grade::G8086_2, 125).verified);
  EXPECT_FALSE(s.ac("TCHRYX", Grade::G8086_2, 125).verified);
  EXPECT_FALSE(s.ac("TINVCH", Grade::G8086_1, 100).verified);
  EXPECT_FALSE(s.ac("TCLRL", Grade::G8086_1, 100).verified);
  EXPECT_FALSE(s.ac("TCLRH", Grade::G8086_1, 100).verified);
  EXPECT_TRUE(s.ac("TCLRH", Grade::G8086, 200).verified);
  EXPECT_FALSE(s.ac("TCLCL", Grade::G8086_1, 100).verified);
  EXPECT_TRUE(s.ac("TCLCL", Grade::G8086, 200).verified);
}

TEST(SpecAc, Errors) {
  const Spec& s = spec();
  EXPECT_THROW(s.ac("TNOPE", Grade::G8086, 200), SpecError);
  EXPECT_THROW(s.ac("TCLAV", Grade::G8086, 0), SpecError);
  EXPECT_THROW(s.ac("TCLAV", Grade::G8086, -5), SpecError);
}

TEST(SpecDc, Limits) {
  const Spec& s = spec();
  const Limit vil = s.dc("VIL", Grade::G8086);
  EXPECT_DOUBLE_EQ(*vil.min, -0.5);
  EXPECT_DOUBLE_EQ(*vil.max, 0.8);
  EXPECT_EQ(vil.unit, "V");
  EXPECT_EQ(vil.ref, "DS §D.C. Characteristics");
  EXPECT_DOUBLE_EQ(*s.dc("VOL", Grade::G8086).max, 0.45);
  EXPECT_FALSE(s.dc("VOL", Grade::G8086).min);
  EXPECT_DOUBLE_EQ(*s.dc("VOH", Grade::G8086_2).min, 2.4);
  EXPECT_DOUBLE_EQ(*s.dc("ILI", Grade::G8086).min, -10);
  EXPECT_DOUBLE_EQ(*s.dc("ILO", Grade::G8086).max, 10);
  EXPECT_EQ(s.dc("ILO", Grade::G8086).unit, "uA");
  EXPECT_DOUBLE_EQ(*s.dc("VCL", Grade::G8086).max, 0.6);
  EXPECT_DOUBLE_EQ(*s.dc("VCH", Grade::G8086).min, 3.9);
  EXPECT_DOUBLE_EQ(*s.dc("CIN", Grade::G8086).max, 15);
  EXPECT_EQ(s.dc("CIO", Grade::G8086).unit, "pF");
}

TEST(SpecDc, IccPerGrade) {
  const Spec& s = spec();
  EXPECT_DOUBLE_EQ(*s.dc("ICC", Grade::G8086).max, 340);
  EXPECT_DOUBLE_EQ(*s.dc("ICC", Grade::G8086_2).max, 350);
  EXPECT_DOUBLE_EQ(*s.dc("ICC", Grade::G8086_1).max, 360);
  EXPECT_EQ(s.dc("ICC", Grade::G8086).unit, "mA");
}

TEST(SpecDc, VccRelativeBoundsHoldAcrossTheSupplyRange) {
  // VIH max = VCC + 0.5 and VCH max = VCC + 1.0, taken at VCC min (4.5 V).
  const Spec& s = spec();
  EXPECT_DOUBLE_EQ(*s.dc("VIH", Grade::G8086).max, 5.0);
  EXPECT_DOUBLE_EQ(*s.dc("VIH", Grade::G8086).min, 2.0);
  EXPECT_DOUBLE_EQ(*s.dc("VCH", Grade::G8086_1).max, 5.5);
}

TEST(SpecDc, WholeTableIsUnverified) {
  for (const char* sym : {"VIL", "VIH", "VOL", "VOH", "ICC", "ILI", "ILO", "VCL", "VCH", "CIN",
                          "CIO"}) {
    for (Grade g : kAllGrades) EXPECT_FALSE(spec().dc(sym, g).verified) << sym;
  }
}

TEST(SpecDc, UnknownSymbol) { EXPECT_THROW(spec().dc("VXX", Grade::G8086), SpecError); }

TEST(SpecAbsMax, Ratings) {
  const Spec& s = spec();
  const Limit v = s.abs_max("V_PIN");
  EXPECT_DOUBLE_EQ(*v.min, -1.0);
  EXPECT_DOUBLE_EQ(*v.max, 7.0);
  EXPECT_EQ(v.unit, "V");
  EXPECT_FALSE(v.verified);
  EXPECT_EQ(v.ref, "DS §Absolute Maximum Ratings");
  EXPECT_DOUBLE_EQ(*s.abs_max("TSTG").min, -65);
  EXPECT_DOUBLE_EQ(*s.abs_max("TSTG").max, 150);
  EXPECT_DOUBLE_EQ(*s.abs_max("TA_BIAS").max, 70);
  EXPECT_DOUBLE_EQ(*s.abs_max("PD").max, 2.5);
  EXPECT_FALSE(s.abs_max("PD").min);
  EXPECT_THROW(s.abs_max("VCC"), SpecError);
}

TEST(SpecDecode, TablesMatchBrief) {
  const DecodeTables& d = spec().decode();
  EXPECT_EQ(d.status[0b000], "int_ack");
  EXPECT_EQ(d.status[0b001], "io_read");
  EXPECT_EQ(d.status[0b010], "io_write");
  EXPECT_EQ(d.status[0b011], "halt");
  EXPECT_EQ(d.status[0b100], "code");
  EXPECT_EQ(d.status[0b101], "mem_read");
  EXPECT_EQ(d.status[0b110], "mem_write");
  EXPECT_EQ(d.status[0b111], "passive");
  EXPECT_EQ(d.segment[0b00], "ES");
  EXPECT_EQ(d.segment[0b01], "SS");
  EXPECT_EQ(d.segment[0b10], "CS_or_none");
  EXPECT_EQ(d.segment[0b11], "DS");
  EXPECT_EQ(d.queue[0b00], "nop");
  EXPECT_EQ(d.queue[0b01], "first_byte");
  EXPECT_EQ(d.queue[0b10], "empty");
  EXPECT_EQ(d.queue[0b11], "subsequent");
  EXPECT_EQ(d.byte_enable[0b00], "word");
  EXPECT_EQ(d.byte_enable[0b01], "high_byte");
  EXPECT_EQ(d.byte_enable[0b10], "low_byte");
  EXPECT_EQ(d.byte_enable[0b11], "none");
}

TEST(Spec, CopiesShareData) {
  const Spec copy = spec();
  EXPECT_EQ(&copy.decode(), &spec().decode());
}

}  // namespace
}  // namespace i8086::spec
