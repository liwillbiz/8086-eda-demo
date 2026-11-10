#include <gtest/gtest.h>

#include "harness.hpp"
#include "i8086/common/errors.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/model/fault_injector.hpp"

namespace i8086::model {
namespace {

using test::Harness;

const spec::Spec& the_spec() {
  static const spec::Spec s = spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml");
  return s;
}

TEST(DieProfile, LoadsGoodDie) {
  const DieProfile die = DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");
  EXPECT_EQ(die.name, "good");
  EXPECT_TRUE(die.faults.empty());
  EXPECT_GT(die.timing_ns.size(), 0u);
}

// The good die must sit inside every AC limit it maps onto, at every grade's fastest clock.
TEST(DieProfile, GoodDieTimingInsideSpecWithMargin) {
  const DieProfile die = DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");
  for (spec::Grade g : spec::kAllGrades) {
    const double tclcl = the_spec().grade(g).tclcl_min_ns;
    for (const char* sym : {"TCLAV", "TCLRL", "TCLRH"}) {
      const spec::Limit l = the_spec().ac(sym, g, tclcl);
      EXPECT_LT(die.timing_ns.at(sym), *l.max) << sym << " " << spec::to_string(g);
      EXPECT_GE(die.timing_ns.at(sym), *l.min) << sym << " " << spec::to_string(g);
    }
    for (const char* sym : {"TDVCL", "TCLDX", "TRYHCH", "TCHRYX", "TINVCH"}) {
      const spec::Limit l = the_spec().ac(sym, g, tclcl);
      EXPECT_LT(die.timing_ns.at(sym), *l.min) << sym << " " << spec::to_string(g);
    }
    // Strobe widths implied by the delays, with the spec's 1/3 duty cycle.
    const double low = tclcl * 2.0 / 3.0;
    const double trlrh = 2 * tclcl - die.timing_ns.at("TCLRL") + die.timing_ns.at("TCLRH");
    EXPECT_GT(trlrh, *the_spec().ac("TRLRH", g, tclcl).min);
    const double twlwh = 2 * tclcl - die.timing_ns.at("TCVCTV") + die.timing_ns.at("TCVCTX");
    EXPECT_GT(twlwh, *the_spec().ac("TWLWH", g, tclcl).min);
    const double tlhll = low - die.timing_ns.at("TCLLH") + die.timing_ns.at("TCHLL");
    EXPECT_GT(tlhll, *the_spec().ac("TLHLL", g, tclcl).min);
    const double trhav = tclcl - die.timing_ns.at("TCLRH") + die.timing_ns.at("TCLAV");
    EXPECT_GT(trhav, *the_spec().ac("TRHAV", g, tclcl).min);
  }
}

TEST(DieProfile, MissingTimingKeyIsReported) {
  DieProfile die = DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");
  die.timing_ns.erase("TCLRL");
  try {
    make_behavioral(the_spec(), die, {});
    FAIL() << "expected SpecError";
  } catch (const spec::SpecError& e) {
    EXPECT_NE(std::string(e.what()).find("TCLRL"), std::string::npos);
  }
}

TEST(FaultSet, FindsLogicBugs) {
  FaultSet fs{{LogicBug{"inc_clears_cf"}, StuckAt{"AD3", 0, std::nullopt}}};
  EXPECT_TRUE(fs.has_logic_bug("inc_clears_cf"));
  EXPECT_FALSE(fs.has_logic_bug("shift_count_masked"));
  EXPECT_EQ(fs.all<StuckAt>().size(), 1u);
}

TEST(Behavioral, IsWrappedInFaultInjectorWithAllFaults) {
  DieProfile die = DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");
  die.faults.faults.emplace_back(StuckAt{"AD3", 1, std::nullopt});
  auto dut = make_behavioral(the_spec(), die, FaultSet{{LogicBug{"inc_clears_cf"}}});
  auto* fi = dynamic_cast<FaultInjector*>(dut.get());
  ASSERT_NE(fi, nullptr);
  EXPECT_EQ(fi->faults().faults.size(), 2u);
}

TEST(Behavioral, FirstEdgeReportsEveryOutputPin) {
  auto dut = make_behavioral(the_spec(), DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml"), {});
  const auto ev = dut->clock_edge(Edge::Falling);
  std::size_t outputs = 0;
  for (const spec::Pin& p : the_spec().pins()) {
    if (p.dir == spec::Dir::Out || p.dir == spec::Dir::Io) ++outputs;
  }
  EXPECT_EQ(ev.size(), outputs);
}

// ---- LogicBug hooks inside the core

TEST(LogicBug, IncClearsCfBreaksD2) {
  Harness h(FaultSet{{LogicBug{"inc_clears_cf"}}});
  h.boot({0xF9, 0x40});  // STC ; INC AX
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  EXPECT_FALSE(h.arch().flags & 0x0001);
}

TEST(LogicBug, ShiftCountMaskedBreaksD8) {
  Harness h(FaultSet{{LogicBug{"shift_count_masked"}}});
  h.boot({0xB8, 0x01, 0x00, 0xB1, 0x21, 0xD3, 0xE0});  // MOV AX,1 ; MOV CL,33 ; SHL AX,CL
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  EXPECT_EQ(h.arch().ax, 2);  // 33 & 31 = 1
}

TEST(LogicBug, UnknownNameIsNotImplemented) {
  EXPECT_THROW(Harness(FaultSet{{LogicBug{"no_such_bug"}}}), NotImplemented);
}

// ---- input sampling: setup/hold windows turn into wrong data or extra wait states

// First fetch at FFFF0h returns EA 00; inspect the queue right after it commits.
std::array<uint8_t, 2> first_fetch_bytes(Harness& h) {
  h.boot({});
  h.reset();
  while (h.txns.empty()) h.clock();
  const ArchState a = h.arch();
  EXPECT_EQ(a.queue_len, 2);
  return {a.queue[0], a.queue[1]};
}

TEST(Sampling, DataInsideSetupWindowLatchesCorrectly) {
  Harness h;
  h.data_changed_ps = -static_cast<int64_t>(h.die.timing_ns.at("TDVCL") * 1000) - 1000;
  EXPECT_EQ(first_fetch_bytes(h), (std::array<uint8_t, 2>{0xEA, 0x00}));
}

TEST(Sampling, DataSetupViolationCorruptsData) {
  Harness h;
  h.data_changed_ps = -static_cast<int64_t>(h.die.timing_ns.at("TDVCL") * 1000) + 1000;
  EXPECT_EQ(first_fetch_bytes(h), (std::array<uint8_t, 2>{0x15, 0xFF}));
}

TEST(Sampling, DataHoldViolationCorruptsData) {
  const int64_t hold = 4000;  // good.yaml TCLDX
  Harness ok;
  ok.data_release_ps = hold + 1000;
  EXPECT_EQ(first_fetch_bytes(ok), (std::array<uint8_t, 2>{0xEA, 0x00}));
  Harness bad;
  bad.data_release_ps = hold - 1000;
  EXPECT_EQ(first_fetch_bytes(bad), (std::array<uint8_t, 2>{0x15, 0xFF}));
}

// The hold window is measured from the latching edge using the real edge spacing the
// tester's change times reveal, not an assumed duty cycle (here CLK is low 110 of 200 ns).
TEST(Sampling, HoldCheckUsesTheTestersEdgeSpacing) {
  const int64_t hold = 4000;  // good.yaml TCLDX
  Harness ok;
  ok.clk_high_ns = 90;
  ok.data_release_ps = hold + 1000;
  EXPECT_EQ(first_fetch_bytes(ok), (std::array<uint8_t, 2>{0xEA, 0x00}));
  Harness bad;
  bad.clk_high_ns = 90;
  bad.data_release_ps = hold - 1000;
  EXPECT_EQ(first_fetch_bytes(bad), (std::array<uint8_t, 2>{0x15, 0xFF}));
}

// The model derates its own setup/hold windows with ElectricalModel's k(VCC,T); the tester
// derates the output delays, so those stay nominal here.
TEST(Sampling, SetupWindowIsDeratedOutputDelaysAreNot) {
  auto make = [](double vcc) {
    auto h = std::make_unique<Harness>();
    h->die.electrical["derate_alpha"] = 1.0;
    h->die.electrical["derate_vt_v"] = 1.0;
    h->die.electrical["derate_vnom_v"] = 5.0;
    h->dut = make_behavioral(h->spec, h->die, {});
    h->dut->set_conditions({vcc, 25.0, h->tclcl_ns});  // k = 4 / (vcc - 1)
    h->data_changed_ps = -static_cast<int64_t>(h->die.timing_ns.at("TDVCL") * 1000) - 1000;
    return h;
  };
  auto nominal = make(5.0);
  EXPECT_EQ(first_fetch_bytes(*nominal), (std::array<uint8_t, 2>{0xEA, 0x00}));
  auto slow = make(3.0);  // k = 2: 3 ns setup becomes 6 ns, so 4 ns is too late
  EXPECT_EQ(first_fetch_bytes(*slow), (std::array<uint8_t, 2>{0x15, 0xFF}));
  for (const test::EdgeLog& e : slow->log) {
    for (const PinEvent& p : e.events) {
      if (p.pin == slow->pin("ALE") && p.value == Logic::L1) {
        EXPECT_EQ(p.delay_ps, static_cast<int64_t>(slow->die.timing_ns.at("TCLLH") * 1000));
      }
    }
  }
  auto h = make(5.0);
  EXPECT_THROW(h->dut->set_conditions({1.0, 25.0, 200.0}), std::domain_error);
}

TEST(Sampling, ReadySetupViolationAddsAWaitState) {
  Harness ok;
  ok.wait_states = 1;
  ok.ready_changed_ps = -static_cast<int64_t>(ok.die.timing_ns.at("TRYHCH") * 1000) - 1000;
  first_fetch_bytes(ok);
  EXPECT_EQ(ok.txns.front().waits, 1);

  Harness late;
  late.wait_states = 1;
  late.ready_changed_ps = -static_cast<int64_t>(late.die.timing_ns.at("TRYHCH") * 1000) + 1000;
  first_fetch_bytes(late);
  EXPECT_EQ(late.txns.front().waits, 2);
}

// ---- features owned by later milestones report NotImplemented (Error, not Fail)

TEST(Behavioral, MaxModeIsNotImplementedYet) {
  Harness h;
  h.force["MN_MX_N"] = Logic::L0;
  h.boot({});
  h.reset();
  h.clock();  // RESET release
  EXPECT_THROW(h.clock(), NotImplemented);
}

TEST(Behavioral, HoldIsNotImplementedYet) {
  Harness h;
  h.boot({});
  h.reset();
  h.clock();
  h.force["HOLD"] = Logic::L1;
  EXPECT_THROW(h.clock(), NotImplemented);
}

}  // namespace
}  // namespace i8086::model
