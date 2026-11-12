#include <gtest/gtest.h>

#include <cmath>

#include "fake_device.hpp"
#include "i8086/tester/search.hpp"
#include "i8086/tester/sim_tester.hpp"

namespace i8086::tester {
namespace {

using model::BusCycleType;
using testing::FakeLog;
using testing::FakeOp;
using testing::FakeTiming;
using Kind = FakeOp::Kind;

const spec::Spec& spec() {
  static const spec::Spec s = spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml");
  return s;
}

struct Rig {
  std::shared_ptr<FakeLog> log = std::make_shared<FakeLog>();
  std::unique_ptr<SimTester> t;
};

Rig make_rig(std::vector<FakeOp> ops, model::DieProfile die = {}, FakeTiming ft = {}) {
  Rig r;
  r.t = std::make_unique<SimTester>(
      spec(), std::make_unique<testing::FakeDevice>(spec(), std::move(ops), r.log, ft),
      ElectricalModel(std::move(die)));
  return r;
}

// Fetch the reset vector word, store it +1 at 00100h, read it back, OUT its low byte, halt.
std::vector<FakeOp> program() {
  return {{Kind::Fetch, 0xFFFF0, true, 0, false},
          {Kind::Write, 0x00100, true, 1, true},
          {Kind::Read, 0x00100, true, 0, false},
          {Kind::IoWrite, 0x0010, false, 0, true},
          {Kind::Halt, 0, true, 0, false}};
}

MemoryImage image() {
  MemoryImage img;
  const uint8_t reset[] = {0x34, 0x12};
  img.load(0xFFFF0, reset);
  const uint8_t data[] = {0xCD, 0xAB};
  img.load(0x00200, data);
  return img;
}

void power_up(SimTester& t, double vcc = 5.0) {
  LevelSet l;
  l.vcc = vcc;
  t.set_levels(l);
  t.set_temperature(25);
  t.power(true);
}

TEST(SimTester, MemoryEmulationRunsProgram) {
  Rig r = make_rig(program());
  power_up(*r.t);
  const FunctionalResult res = r.t->run_program(image(), RunOpts{});
  EXPECT_TRUE(res.pass);
  ASSERT_EQ(r.log->reads.size(), 2u);
  EXPECT_EQ(r.log->reads[0], 0x1234);
  EXPECT_EQ(r.log->reads[1], 0x1235);  // the write landed in emulated RAM

  const auto& tx = r.t->last_trace().transactions;
  ASSERT_EQ(tx.size(), 5u);
  EXPECT_EQ(tx[0].type, BusCycleType::CodeFetch);
  EXPECT_EQ(tx[0].address, 0xFFFF0u);
  EXPECT_EQ(tx[0].data, 0x1234);
  EXPECT_FALSE(tx[0].bhe_n);
  EXPECT_EQ(tx[0].start_clk, 11u);  // RESET low from CLK 4, first T1 seven CLKs later
  EXPECT_EQ(tx[0].wait_states, 0);
  EXPECT_EQ(tx[1].type, BusCycleType::MemWrite);
  EXPECT_EQ(tx[1].address, 0x00100u);
  EXPECT_EQ(tx[1].data, 0x1235);
  EXPECT_EQ(tx[1].start_clk, 15u);
  EXPECT_EQ(tx[2].type, BusCycleType::MemRead);
  EXPECT_EQ(tx[2].data, 0x1235);
  EXPECT_EQ(tx[3].type, BusCycleType::IoWrite);
  EXPECT_EQ(tx[3].address, 0x0010u);
  EXPECT_TRUE(tx[3].bhe_n);
  EXPECT_EQ(tx[3].data & 0xFF, 0x35);
  EXPECT_EQ(tx[4].type, BusCycleType::Halt);
  EXPECT_EQ(res.cycles, tx[4].start_clk + 1);
  EXPECT_EQ(r.t->last_trace().cycles.size(), res.cycles);
}

TEST(SimTester, MemoryEmulationTimesOutWithoutHalt) {
  Rig r = make_rig({{Kind::Fetch, 0xFFFF0, true, 0, false}});
  power_up(*r.t);
  RunOpts opts;
  opts.max_cycles = 50;
  const FunctionalResult res = r.t->run_program(image(), opts);
  EXPECT_FALSE(res.pass);
  EXPECT_EQ(res.cycles, 50u);
  opts.stop_on_halt = false;
  Rig r2 = make_rig({{Kind::Fetch, 0xFFFF0, true, 0, false}});
  power_up(*r2.t);
  EXPECT_TRUE(r2.t->run_program(image(), opts).pass);
}

TEST(SimTester, WaitStatesFromReady) {
  for (int n : {0, 1, 3}) {
    Rig r = make_rig(program());
    power_up(*r.t);
    RunOpts opts;
    opts.wait_states = n;
    ASSERT_TRUE(r.t->run_program(image(), opts).pass) << n;
    EXPECT_EQ(r.log->wait_states, std::vector<int>(4, n));
    EXPECT_EQ(r.log->reads[0], 0x1234) << n;
    for (const auto& tx : r.t->last_trace().transactions) {
      if (tx.type != BusCycleType::Halt) {
        EXPECT_EQ(tx.wait_states, n) << n;
      }
    }
  }
}

TEST(SimTester, ByteLanes) {
  Rig r = make_rig({{Kind::Read, 0x00201, false, 0, false},
                    {Kind::Read, 0x00200, false, 0, false},
                    {Kind::Read, 0x00200, true, 0, false},
                    {Kind::Read, 0x00300, true, 0, false},  // unmapped: the bus floats
                    {Kind::Halt, 0, true, 0, false}});
  power_up(*r.t);
  ASSERT_TRUE(r.t->run_program(image(), RunOpts{}).pass);
  const auto& tx = r.t->last_trace().transactions;
  ASSERT_EQ(tx.size(), 5u);
  EXPECT_FALSE(tx[0].bhe_n);
  EXPECT_EQ(tx[0].data, 0xAB00);
  EXPECT_TRUE(tx[1].bhe_n);
  EXPECT_EQ(tx[1].data, 0x00CD);
  EXPECT_EQ(tx[2].data, 0xABCD);
  ASSERT_EQ(r.log->reads.size(), 4u);
  EXPECT_EQ(r.log->reads[0], 0xAB);
  EXPECT_EQ(r.log->reads[1], 0xCD);
  EXPECT_EQ(r.log->reads[2], 0xABCD);
  EXPECT_EQ(r.log->reads[3], std::nullopt);
}

// The B3 primitive: move the read-data drive edge until the fake's 14 ns setup breaks.
TEST(SimTester, SearchFindsReadDataSetupEdge) {
  const auto run_at = [](double drive_ns) {
    Rig r = make_rig(program());
    power_up(*r.t);
    TimingSet ts;
    ts.period_ns = 200;
    ts.pins["AD"] = PinTiming{DriveFormat::NRZ, drive_ns, 0};
    r.t->set_timing(0, ts);
    r.t->run_program(image(), RunOpts{});
    return !r.log->reads.empty() && r.log->reads[0] == 0x1234;
  };
  const double edge = search_pass_fail(run_at, 0.0, 199.0, 0.01);
  EXPECT_LE(edge, 186.0);
  EXPECT_NEAR(edge, 186.0, 0.01);  // setup = 200 ns - drive edge
}

TEST(SimTester, MeasureTimeAndDefaultDuty) {
  Rig r = make_rig(program());
  power_up(*r.t);
  ASSERT_TRUE(r.t->run_program(image(), RunOpts{}).pass);
  SimTester& t = *r.t;
  const double high = t.measure_time({"CLK", Transition::Rise, 0}, {"CLK", Transition::Fall, 1});
  const double low = t.measure_time({"CLK", Transition::Fall, 0}, {"CLK", Transition::Rise, 0});
  EXPECT_NEAR(high + low, 200.0, 1e-9);
  for (spec::Grade g : spec::kAllGrades) {
    EXPECT_GE(high, *spec().ac("TCHCL", g, 200).min);
    EXPECT_GE(low, *spec().ac("TCLCH", g, 200).min);
  }
  EXPECT_NEAR(t.measure_time({"CLK", Transition::Fall, 11}, {"ALE", Transition::Rise, 0}), 15.0, 1e-9);
  EXPECT_NEAR(t.measure_time({"CLK", Transition::Fall, 11}, {"A19", Transition::Rise, 0}), 30.0, 1e-9);
  // Fall #0 is ALE leaving X at power-on; #1 ends the first ALE pulse.
  EXPECT_NEAR(t.measure_time({"ALE", Transition::Rise, 0}, {"ALE", Transition::Fall, 1}),
              low + 10.0 - 15.0, 1e-9);
  EXPECT_THROW(t.measure_time({"ALE", Transition::Rise, 99}, {"ALE", Transition::Fall, 0}),
               std::out_of_range);
}

TEST(SimTester, DeratingScalesDutDelays) {
  model::DieProfile die;
  die.electrical = {{"derate_alpha", 1.0}, {"derate_vt_v", 1.0}, {"derate_vnom_v", 5.0}};
  Rig r = make_rig(program(), die);
  power_up(*r.t, 4.5);
  ASSERT_TRUE(r.t->run_program(image(), RunOpts{}).pass);
  EXPECT_NEAR(r.t->measure_time({"CLK", Transition::Fall, 11}, {"A19", Transition::Rise, 0}),
              std::round(30'000 * 4.0 / 3.5) / 1000, 1e-9);
  ASSERT_FALSE(r.log->conditions.empty());
  EXPECT_DOUBLE_EQ(r.log->conditions.back().vcc_v, 4.5);
  EXPECT_DOUBLE_EQ(r.log->conditions.back().temp_c, 25.0);
  EXPECT_DOUBLE_EQ(r.log->conditions.back().tclcl_ns, 200.0);
}

// B5: LevelSet::cl_pf adds ElectricalModel::load_delay_ps to every driven DUT edge.
TEST(SimTester, LoadCapacitanceDelaysDrivenEdges) {
  model::DieProfile die;
  die.electrical = {{"load_slope_ns_per_pf", 0.05}};
  Rig r = make_rig(program(), die);
  LevelSet l;
  l.cl_pf = 100;
  r.t->set_levels(l);
  r.t->set_temperature(25);
  r.t->power(true);
  ASSERT_TRUE(r.t->run_program(image(), RunOpts{}).pass);
  EXPECT_NEAR(r.t->measure_time({"CLK", Transition::Fall, 11}, {"A19", Transition::Rise, 0}), 30.0 + 5.0, 1e-9);
}

// C7: a CLK level on the wrong side of the die's CLK threshold is no edge to the DUT.
TEST(SimTester, ClkLevelsMustCrossTheClkThreshold) {
  model::DieProfile die;
  die.electrical = {{"vin_threshold_v.CLK", 2.0}, {"vin_band_v.CLK", 0.2}};
  const auto run_with = [&](double vch, double vcl) {
    Rig r = make_rig(program(), die);
    LevelSet l;
    l.vch = vch;
    l.vcl = vcl;
    r.t->set_levels(l);
    r.t->set_temperature(25);
    r.t->power(true);
    RunOpts opts;
    opts.max_cycles = 100;
    const bool pass = r.t->run_program(image(), opts).pass;
    return std::pair{pass, r.log->reads.size()};
  };
  EXPECT_EQ(run_with(3.9, 0.6), (std::pair{true, std::size_t{2}}));
  EXPECT_EQ(run_with(2.05, 0.6), (std::pair{false, std::size_t{0}}));  // inside the band: X
  EXPECT_EQ(run_with(3.9, 1.95), (std::pair{false, std::size_t{0}}));
  EXPECT_EQ(run_with(2.1, 1.89), (std::pair{true, std::size_t{2}}));
}

TEST(SimTester, RunProgramPreconditions) {
  Rig r = make_rig(program());
  EXPECT_THROW(r.t->run_program(image(), RunOpts{}), std::logic_error);  // power off
  power_up(*r.t);
  EXPECT_THROW(r.t->run_program(MemoryImage{}, RunOpts{}), std::invalid_argument);
  RunOpts bad;
  bad.wait_states = -1;
  EXPECT_THROW(r.t->run_program(image(), bad), std::invalid_argument);
  RunOpts reset_pin;
  reset_pin.stimuli.push_back({.pin = "RESET", .high = true, .clk = 10, .at_ns = {}});
  EXPECT_THROW(r.t->run_program(image(), reset_pin), std::invalid_argument);
  RunOpts late;
  late.stimuli.push_back({.pin = "NMI", .high = true, .clk = 10, .at_ns = 1000.0});
  EXPECT_THROW(r.t->run_program(image(), late), std::invalid_argument);
}

TEST(SimTester, HaltWaitsForScheduledStimuli) {
  Rig r = make_rig(program());
  power_up(*r.t);
  const FunctionalResult plain = r.t->run_program(image(), RunOpts{});
  ASSERT_TRUE(plain.pass);
  RunOpts opts;
  opts.max_cycles = plain.cycles + 50;
  opts.stimuli.push_back({.pin = "TEST_N", .high = true, .clk = plain.cycles + 20, .at_ns = {}});
  const FunctionalResult res = r.t->run_program(image(), opts);
  EXPECT_TRUE(res.pass);  // the fake stays halted: the run ends in the CLK after the stimulus
  EXPECT_EQ(res.cycles, plain.cycles + 22);
}

TEST(SimTester, LevelsGuardedByAbsMax) {
  Rig r = make_rig({});
  LevelSet l;
  EXPECT_NO_THROW(r.t->set_levels(l));
  l.vcc = 7.5;
  EXPECT_THROW(r.t->set_levels(l), std::out_of_range);
  l = LevelSet{};
  l.vih = 7.01;
  EXPECT_THROW(r.t->set_levels(l), std::out_of_range);
  l = LevelSet{};
  l.vil = -1.2;
  EXPECT_THROW(r.t->set_levels(l), std::out_of_range);
  l = LevelSet{};
  l.vcc = std::nan("");
  EXPECT_THROW(r.t->set_levels(l), std::out_of_range);
  l = LevelSet{};
  l.vch = 7.5;
  EXPECT_THROW(r.t->set_levels(l), std::out_of_range);
  l = LevelSet{};
  l.vcl = -1.5;
  EXPECT_THROW(r.t->set_levels(l), std::out_of_range);
  l = LevelSet{};
  l.cl_pf = -1;
  EXPECT_THROW(r.t->set_levels(l), std::out_of_range);
}

TEST(SimTester, TemperatureGuardedByAbsMax) {
  Rig r = make_rig({});
  EXPECT_NO_THROW(r.t->set_temperature(100));  // storage range while unpowered
  EXPECT_THROW(r.t->power(true), std::out_of_range);
  EXPECT_THROW(r.t->set_temperature(160), std::out_of_range);
  r.t->set_temperature(70);
  r.t->power(true);
  EXPECT_THROW(r.t->set_temperature(71), std::out_of_range);
  EXPECT_NO_THROW(r.t->set_temperature(0));
}

TEST(SimTester, TimingSetValidation) {
  Rig r = make_rig({});
  TimingSet ts;
  EXPECT_NO_THROW(r.t->set_timing(0, ts));
  EXPECT_THROW(r.t->set_timing(-1, ts), std::invalid_argument);
  ts.period_ns = 0;
  EXPECT_THROW(r.t->set_timing(0, ts), std::invalid_argument);
  ts = TimingSet{};
  ts.clk_high_ns = 250;
  EXPECT_THROW(r.t->set_timing(0, ts), std::invalid_argument);
  ts = TimingSet{};
  ts.pins["AD"] = PinTiming{DriveFormat::NRZ, 200, 0};
  EXPECT_THROW(r.t->set_timing(0, ts), std::invalid_argument);
  ts = TimingSet{};
  ts.pins["VCC"] = PinTiming{};
  EXPECT_THROW(r.t->set_timing(0, ts), std::invalid_argument);
  ts = TimingSet{};
  ts.pins["NOPE"] = PinTiming{};
  EXPECT_THROW(r.t->set_timing(0, ts), spec::SpecError);
}

constexpr const char* kResetPat = R"(pattern reset_vector
pins    RESET READY INTR NMI HOLD MN_MX_N  ALE RD_N WR_N M_IO_N  AD[15:0]           A[19:16]
timing  0 period=TCLCL drive=0ns  strobe=TCLAV.max+5ns grade=8086-2
v 0     1     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 4
v 0     0     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 7
v 0     0     1     0    0   0  1      H    H    H    H     HHHHHHHHHHHHLLLL    HHHH   # T1: addr FFFF0h
)";

TEST(SimTester, VectorModeResetPattern) {
  Rig r = make_rig(program());
  power_up(*r.t);
  const FunctionalResult res = r.t->run_pattern(Pattern::parse(kResetPat));
  EXPECT_TRUE(res.pass) << (res.fails.empty() ? "" : res.fails[0].pin);
  EXPECT_EQ(res.cycles, 12u);
  EXPECT_EQ(r.log->reset_released_clk, 5u);  // the fake counts rising edges from 1
}

TEST(SimTester, VectorModeReportsFailingStrobes) {
  Pattern p = Pattern::parse(kResetPat);
  p.timings[0].attrs["strobe"] = "TCLAV.min+10ns";  // 20 ns: before the fake's 30 ns TCLAV
  Rig r = make_rig(program());
  power_up(*r.t);
  const FunctionalResult res = r.t->run_pattern(p);
  EXPECT_FALSE(res.pass);
  ASSERT_EQ(res.fails.size(), 20u);  // AD15-0 and A19-16 still floating
  EXPECT_EQ(res.fails[0].vector, 11u);
  EXPECT_EQ(res.fails[0].pin, "AD15");
  EXPECT_EQ(res.fails[0].expected, 'H');
  EXPECT_EQ(res.fails[0].actual, model::Logic::Z);
  EXPECT_EQ(res.fails[0].strobe_ps, 11 * 200'000 + 20'000);
}

TEST(SimTester, VectorModeHonoursDriveLevels) {
  Rig r = make_rig(program());
  LevelSet l;
  l.vih = 1.2;  // below the die's input threshold: RESET never reads high
  r.t->set_levels(l);
  r.t->power(true);
  EXPECT_FALSE(r.t->run_pattern(Pattern::parse(kResetPat)).pass);
}

TEST(SimTester, VectorModeCompareLevelsUseLoadedOutputVoltage) {
  model::DieProfile weak;
  weak.electrical = {{"voh_drop_v", 1.0}, {"ron_high_ohm", 5000}};  // VOH = 4.0 - 2.0 V
  Rig r = make_rig(program(), weak);
  LevelSet l;
  l.voh = 2.4;
  r.t->set_levels(l);
  r.t->power(true);
  const FunctionalResult res = r.t->run_pattern(Pattern::parse(kResetPat));
  EXPECT_FALSE(res.pass);
  ASSERT_FALSE(res.fails.empty());
  EXPECT_EQ(res.fails[0].expected, 'H');
  EXPECT_EQ(res.fails[0].actual, model::Logic::X);
}

TEST(SimTester, VectorModeReturnToZeroDrive) {
  Rig r = make_rig({});
  power_up(*r.t);
  const Pattern p = Pattern::parse(
      "pattern rz\npins RESET NMI\ntiming 0 period=100ns drive=0ns drive.NMI=40ns fmt.NMI=RZ\n"
      "v 0 1 1\nv 0 1 0\nv 0 1 1\n");
  ASSERT_TRUE(r.t->run_pattern(p).pass);
  EXPECT_DOUBLE_EQ(r.t->measure_time({"CLK", Transition::Fall, 0}, {"NMI", Transition::Rise, 0}), 40.0);
  EXPECT_DOUBLE_EQ(r.t->measure_time({"NMI", Transition::Rise, 0}, {"NMI", Transition::Fall, 0}), 60.0);
  EXPECT_DOUBLE_EQ(r.t->measure_time({"CLK", Transition::Fall, 0}, {"NMI", Transition::Rise, 1}), 240.0);
}

TEST(SimTester, VectorModeRejectsBadPatterns) {
  Rig r = make_rig({});
  power_up(*r.t);
  EXPECT_THROW(r.t->run_pattern(Pattern::parse("pattern p\npins ALE\nv 0 1\n")), std::invalid_argument);
  EXPECT_THROW(r.t->run_pattern(Pattern::parse("pattern p\npins RESET\nv 0 H\n")), std::invalid_argument);
  EXPECT_THROW(r.t->run_pattern(Pattern::parse("pattern p\npins CLK\nv 0 1\n")), std::invalid_argument);
  EXPECT_THROW(r.t->run_pattern(Pattern::parse("pattern p\npins VCC\nv 0 1\n")), std::invalid_argument);
  EXPECT_THROW(r.t->run_pattern(Pattern::parse("pattern p\npins RESET\ntiming 0 strobe=TCLAV.max\nv 0 1\n")),
               std::invalid_argument);  // spec symbol without grade=
  EXPECT_THROW(r.t->run_pattern(Pattern::parse("pattern p\npins RESET\ntiming 0 drive=250ns\nv 0 1\n")),
               std::invalid_argument);
  EXPECT_THROW(r.t->run_pattern(Pattern::parse("pattern p\npins RESET\ntiming 0 fmt=XX\nv 0 1\n")),
               std::invalid_argument);
}

TEST(SimTester, PatternPeriodFromSpecGrade) {
  Rig r = make_rig({});
  power_up(*r.t);
  ASSERT_TRUE(r.t->run_pattern(Pattern::parse(
                    "pattern p\npins RESET\ntiming 0 period=TCLCL.min grade=8086-1\nv 0 1 repeat 3\n"))
                  .pass);
  EXPECT_DOUBLE_EQ(r.t->measure_time({"CLK", Transition::Fall, 0}, {"CLK", Transition::Fall, 2}), 200.0);
}

TEST(SimTester, PmuMeasurements) {
  model::DieProfile die;
  die.electrical = {{"icc_base_ma", 280}, {"vol_offset_v", 0.1}, {"ron_low_ohm", 40}};
  die.faults.faults.emplace_back(std::in_place_type<model::Leaky>, "NMI", 25.0);
  Rig r = make_rig(program(), die);
  EXPECT_THROW(r.t->measure_icc(), std::logic_error);
  power_up(*r.t);
  ASSERT_TRUE(r.t->run_program(image(), RunOpts{}).pass);
  EXPECT_DOUBLE_EQ(r.t->force_i_measure_v("RD_N", -0.4), 5.0);  // RD# idles high after the run
  EXPECT_NEAR(r.t->measure_icc(), 280.025, 1e-9);
  EXPECT_DOUBLE_EQ(r.t->force_v_measure_i("NMI", 5.0), 25.0);
  EXPECT_DOUBLE_EQ(r.t->force_v_measure_i("INTR", 0.0), 0.0);
  EXPECT_NEAR(r.t->force_i_measure_v("AD0", 2.5), 0.2, 1e-12);  // halt cycle drove address 0
  EXPECT_THROW(r.t->force_v_measure_i("RD_N", 0.45), std::domain_error);
  EXPECT_THROW(r.t->force_v_measure_i("NMI", 7.5), std::out_of_range);
  EXPECT_THROW(r.t->force_i_measure_v("NMI", 1.0), std::invalid_argument);
  Rig fresh = make_rig({});
  power_up(*fresh.t);
  EXPECT_THROW(fresh.t->force_i_measure_v("AD0", 1.0), std::domain_error);  // nothing ran: floating
}

}  // namespace
}  // namespace i8086::tester
