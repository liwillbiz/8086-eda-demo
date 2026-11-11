// The M6 margin tests (B1-B5, C1-C7) and the shmoo: green on the good and marginal dies at
// the worst corner, and each test fails on a die pushed just outside the limit it judges.

#include <gtest/gtest.h>

#include <sstream>

#include "i8086/model/device_model.hpp"
#include "i8086/tester/sim_tester.hpp"
#include "i8086/testprog/runner.hpp"
#include "margin.hpp"
#include "shmoo.hpp"

namespace i8086::testprog {
namespace {

const std::vector<std::string> kM6 = {"B1", "B2", "B3", "B4", "B5", "C1",
                                      "C2", "C3", "C4", "C5", "C6", "C7"};
// The margin tests judged at the worst corner: M6 plus B6 (M8).
const std::vector<std::string> kMargin = [] {
  auto v = kM6;
  v.push_back("B6");
  return v;
}();

const spec::Spec& the_spec() {
  static const spec::Spec s = spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml");
  return s;
}

model::DieProfile die(const std::string& name) {
  return model::DieProfile::load(I86_SOURCE_DIR "/spec/dies/" + name + ".yaml");
}

// Runs one case at the worst corner: 8086-1 (fastest clock), 4.5 V, 70 °C.
Verdict run_case(const model::DieProfile& d, const std::string& id, Datalog& log) {
  static const golden::Emulator golden;
  tester::SimTester t(the_spec(), model::make_behavioral(the_spec(), d, {}), tester::ElectricalModel(d));
  tester::LevelSet levels;
  levels.vcc = 4.5;
  t.set_levels(levels);
  t.set_temperature(70);
  t.power(true);
  log.begin(id, {d.name, spec::Grade::G8086_1, 4.5, 70});
  TestContext ctx{t, the_spec(), spec::Grade::G8086_1, 4.5, 70, log, &golden};
  return TestRegistry::global().create(id)->run(ctx);
}

TEST(Margin, M6SetIsRegistered) {
  const auto ids = TestRegistry::global().ids();
  for (const auto& id : kM6) EXPECT_NE(std::ranges::find(ids, id), ids.end()) << id;
}

class InSpecDie : public testing::TestWithParam<const char*> {};

TEST_P(InSpecDie, EveryMarginTestPassesAtTheWorstCorner) {
  const model::DieProfile d = die(GetParam());
  for (const auto& id : kMargin) {
    Datalog log("t");
    EXPECT_EQ(run_case(d, id, log), Verdict::Pass) << id << ": " << [&] {
      for (const auto& r : log.records()) {
        if (r.result != Verdict::Pass) return r.param + " " + r.note;
      }
      return std::string();
    }();
  }
}

INSTANTIATE_TEST_SUITE_P(Dies, InSpecDie, testing::Values("good", "marginal"));

struct OutOfSpec {
  const char* name;
  const char* key;  // "timing.SYM" or an ElectricalModel key
  double value;
  const char* catches;
};

class CatchesOutOfSpecDie : public testing::TestWithParam<OutOfSpec> {};

TEST_P(CatchesOutOfSpecDie, TheJudgingTestFails) {
  model::DieProfile d = die("good");
  const std::string key = GetParam().key;
  if (key.starts_with("timing.")) {
    d.timing_ns[key.substr(7)] = GetParam().value;
  } else {
    d.electrical[key] = GetParam().value;
  }
  Datalog log("t");
  EXPECT_EQ(run_case(d, GetParam().catches, log), Verdict::Fail) << GetParam().name;
}

// Worst corner k = 1.33: each value lands just outside its 8086-1 limit once derated.
INSTANTIATE_TEST_SUITE_P(
    Dies, CatchesOutOfSpecDie,
    testing::Values(OutOfSpec{"slow_critical_path", "timing.TCLCL_CRIT", 80, "B1"},
                    OutOfSpec{"long_data_setup", "timing.TDVCL", 4.5, "B3"},
                    OutOfSpec{"long_ready_setup", "timing.TRYHCH", 40, "B4"},
                    OutOfSpec{"slow_address", "timing.TCLAV", 37, "B5"},
                    OutOfSpec{"long_int_setup", "timing.TINVCH", 12, "B6"},
                    OutOfSpec{"weak_pull_down", "vol_offset_v", 0.4, "C1"},
                    OutOfSpec{"high_input_threshold", "vin_threshold_v", 2.0, "C2"},
                    OutOfSpec{"leaky", "leak_ua", 12, "C3"},
                    OutOfSpec{"leaky_outputs", "leak_ua", 12, "C4"},
                    OutOfSpec{"hungry", "icc_base_ma", 400, "C5"},
                    OutOfSpec{"high_clk_threshold", "vin_threshold_v.CLK", 3.9, "C7"}),
    [](const testing::TestParamInfo<OutOfSpec>& p) { return std::string(p.param.name); });

// Searches reuse one DUT: a run that broke it (bad CLK levels, garbage read data) must not
// leak into the next run, which starts with RESET.
TEST(Margin, AFailedRunDoesNotBreakTheNext) {
  static const golden::Emulator golden;
  const model::DieProfile d = die("good");
  tester::SimTester t(the_spec(), model::make_behavioral(the_spec(), d, {}), tester::ElectricalModel(d));
  t.set_temperature(25);
  t.power(true);
  Datalog log("t");
  TestContext ctx{t, the_spec(), spec::Grade::G8086, 5.0, 25, log, &golden};
  setup_functional(ctx);
  const auto with = [&](double vch, double vcl, double data_setup) {
    tester::LevelSet l;
    l.vch = vch;
    l.vcl = vcl;
    t.set_levels(l);
    tester::TimingSet ts = functional_timing(the_spec(), ctx.grade);
    ts.pins["AD"].drive_ns = ts.period_ns - data_setup;
    t.set_timing(0, ts);
    return rom_outcome(ctx, "b1_checksum").pass;
  };
  EXPECT_TRUE(with(3.9, 0.6, 100));
  EXPECT_FALSE(with(3.9, 1.5, 100));  // CLK low inside the die's band: the clock stops
  EXPECT_TRUE(with(3.9, 0.6, 100));
  EXPECT_FALSE(with(1.0, 0.6, 100));  // CLK high never crosses the threshold
  EXPECT_TRUE(with(3.9, 0.6, 100));
  EXPECT_FALSE(with(3.9, 0.6, 0.5));  // read data too late: the EU executes garbage
  EXPECT_TRUE(with(3.9, 0.6, 100));
}

TEST(Margin, FindEdgeIsEmptyWhenTheStartPointFails) {
  EXPECT_FALSE(find_edge([](double) { return false; }, 0, 10, 0.1));
  EXPECT_NEAR(*find_edge([](double x) { return x < 3.3; }, 0, 10, 0.01), 3.3, 0.01);
  EXPECT_DOUBLE_EQ(*find_edge([](double) { return true; }, 0, 10, 0.1), 10);
}

TEST(Margin, TurnedLimitsKeepProvenance) {
  const spec::Limit l{5.0, 9.0, "ns", "DS", false};
  const spec::Limit lo = at_most_min(l);
  EXPECT_FALSE(lo.min);
  EXPECT_EQ(lo.max, 5.0);
  EXPECT_EQ(lo.ref, "DS");
  EXPECT_FALSE(lo.verified);
  const spec::Limit hi = at_least_max(l);
  EXPECT_EQ(hi.min, 9.0);
  EXPECT_FALSE(hi.max);
}

TEST(Shmoo, SweepFindsTheFmaxEdgeOutsideTheSpecBoxes) {
  ShmooConfig cfg;
  cfg.spec_path = I86_SOURCE_DIR "/spec/spec.yaml";
  cfg.die_path = I86_SOURCE_DIR "/spec/dies/good.yaml";
  cfg.vcc_v = {4.0, 5.0};
  cfg.mhz = {12.5, 5};
  const ShmooResult r = run_shmoo(cfg);
  EXPECT_EQ(r.die, "good");
  ASSERT_EQ(r.cells.size(), 4u);
  EXPECT_EQ(r.mhz, (std::vector<double>{5, 12.5}));
  EXPECT_TRUE(r.at(0, 0).pass);
  EXPECT_FALSE(r.at(0, 1).pass);  // 62 ns critical path x k(4.0 V) = 95 ns > 80 ns
  EXPECT_EQ(r.at(0, 1).failed_rom, "boot");
  EXPECT_TRUE(r.at(1, 0).pass);
  EXPECT_TRUE(r.at(1, 1).pass);
  EXPECT_EQ(r.box_fails(), 0);
}

ShmooResult synthetic() {
  ShmooResult r;
  r.die = "demo";
  r.vcc_v = {4.5, 5.0};
  r.mhz = {5, 8, 10};
  r.boxes = {{spec::Grade::G8086, 5}, {spec::Grade::G8086_2, 8}, {spec::Grade::G8086_1, 10}};
  for (double v : r.vcc_v) {
    for (double f : r.mhz) r.cells.push_back({v, f, !(v == 4.5 && f == 10), v == 4.5 && f == 10 ? "boot" : "", "x < y"});
  }
  return r;
}

TEST(Shmoo, WritersMarkFailsInsideASpecBox) {
  const ShmooResult r = synthetic();
  EXPECT_EQ(r.box_fails(), 1);

  std::ostringstream csv;
  write_shmoo_csv(csv, r);
  EXPECT_EQ(csv.str(),
            "vcc_v,mhz,tclcl_ns,result,first_fail\n"
            "5.00,5,200.0,pass,\n5.00,8,125.0,pass,\n5.00,10,100.0,pass,\n"
            "4.50,5,200.0,pass,\n4.50,8,125.0,pass,\n4.50,10,100.0,fail,boot\n");

  std::ostringstream ascii;
  write_shmoo_ascii(ascii, r);
  EXPECT_NE(ascii.str().find(" 5.00    [*]  [*]  [*]"), std::string::npos) << ascii.str();
  EXPECT_NE(ascii.str().find(" 4.50    [*]  [*]  [X]"), std::string::npos) << ascii.str();
  EXPECT_NE(ascii.str().find("1 failing cell(s) inside a box"), std::string::npos) << ascii.str();

  std::ostringstream svg;
  write_shmoo_svg(svg, r);
  const std::string s = svg.str();
  EXPECT_TRUE(s.starts_with("<svg xmlns=\"http://www.w3.org/2000/svg\"")) << s;
  EXPECT_TRUE(s.ends_with("</svg>\n"));
  EXPECT_NE(s.find("8086-1 ≤ 10 MHz"), std::string::npos);
  EXPECT_NE(s.find("x &lt; y"), std::string::npos);  // failure text is escaped
  EXPECT_NE(s.find("#b71c1c"), std::string::npos);   // the in-box fail colour
}

}  // namespace
}  // namespace i8086::testprog
