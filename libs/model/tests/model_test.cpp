#include <gtest/gtest.h>

#include <cmath>

#include "harness.hpp"
#include "i8086/common/errors.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/model/fault_injector.hpp"

namespace i8086::model {
namespace {

using test::Harness;
using test::Txn;

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
class DieInsideSpec : public testing::TestWithParam<const char*> {};

TEST_P(DieInsideSpec, TimingInsideSpecWithMargin) {
  const DieProfile die = DieProfile::load(std::string(I86_SOURCE_DIR "/spec/dies/") + GetParam());
  // ElectricalModel's k(VCC,T) at the worst corner, 4.5 V and 70 °C.
  const auto e = [&](const char* key) { return die.electrical.at(key); };
  const double k_worst = std::pow((e("derate_vnom_v") - e("derate_vt_v")) / (4.5 - e("derate_vt_v")), e("derate_alpha")) *
                         (1 + e("derate_beta_per_c") * (70 - 25));
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
    EXPECT_LT(die.timing_ns.at("TCLCL_CRIT") * k_worst, tclcl) << spec::to_string(g);
    EXPECT_LT(die.timing_ns.at("TDVCL") * k_worst, *the_spec().ac("TDVCL", g, tclcl).min) << spec::to_string(g);
  }
}
INSTANTIATE_TEST_SUITE_P(Dies, DieInsideSpec, testing::Values("good.yaml", "marginal.yaml"),
                         [](const testing::TestParamInfo<const char*>& p) {
                           return std::string(p.param).substr(0, std::string(p.param).find('.'));
                         });

// Below the die's internal critical path (TCLCL_CRIT, derated) write data never settles:
// the device drives X on AD in T2-T4 of a write instead of the data.
TEST(Behavioral, WriteDataIsUnknownBelowTheCriticalPath) {
  const std::vector<uint8_t> code{0xB8, 0x34, 0x12, 0xA3, 0x10, 0x00, 0xF4};  // mov ax,1234h; mov [10h],ax; hlt
  const auto write_data = [&](double crit_ns) {
    Harness h;
    h.die.timing_ns["TCLCL_CRIT"] = crit_ns;
    h.dut = make_behavioral(h.spec, h.die, {});
    h.dut->set_conditions({5.0, 25.0, h.tclcl_ns});
    h.boot(code);
    h.reset();
    EXPECT_TRUE(h.run_until_halt());
    for (const test::Txn& t : h.txns) {
      if (t.type == BusCycleType::MemWrite) return t.data;
    }
    ADD_FAILURE() << "no write cycle";
    return uint16_t{0};
  };
  EXPECT_EQ(write_data(150), 0x1234);
  EXPECT_NE(write_data(250), 0x1234);
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

// ---- M8: interrupts, HOLD, HLT

std::vector<const Txn*> of_type(const Harness& h, BusCycleType t) {
  std::vector<const Txn*> v;
  for (const Txn& x : h.txns) {
    if (x.type == t) v.push_back(&x);
  }
  return v;
}

TEST(Behavioral, NmiRisingEdgeWakesHaltOnce) {
  Harness h;
  h.boot({0xC7, 0x06, 0x08, 0x00, 26, 0x00,    // mov [0008h], handler
          0xC7, 0x06, 0x0A, 0x00, 0x00, 0x01,  // mov [000Ah], 0100h
          0xF4, 0x43, 0xF4,                    // hlt; inc bx; hlt
          0x41, 0xCF});                        // handler: inc cx; iret
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  EXPECT_EQ(of_type(h, BusCycleType::Halt).size(), 1u);
  h.force["NMI"] = Logic::L1;
  for (int i = 0; i < 4; ++i) h.clock();
  ASSERT_TRUE(h.run_until_halt());
  for (int i = 0; i < 100; ++i) h.clock();  // NMI held high: no second entry
  EXPECT_EQ(h.arch().cx, 1);
  EXPECT_EQ(h.arch().bx, 1);
  EXPECT_TRUE(h.arch().halted);
  EXPECT_EQ(of_type(h, BusCycleType::InterruptAck).size(), 0u);
}

TEST(Behavioral, IntrRunsTwoIntaCyclesAfterTheStiShadow) {
  auto run = [](bool sti) {
    auto h = std::make_unique<Harness>();
    h->boot({0xC7, 0x06, 0x00, 0x01, 26, 0x00,    // mov [0100h], handler (type 40h)
             0xC7, 0x06, 0x02, 0x01, 0x00, 0x01,  // mov [0102h], 0100h
             uint8_t(sti ? 0xFB : 0x90), 0x90,    // sti (or nop); nop
             0xF4,                                // hlt
             0x58, 0xBA, 0x55, 0x00});            // handler: pop ax; mov dx, 55h; (hlt)
    h->inta_type = 0x40;
    h->force["INTR"] = Logic::L1;
    h->reset();
    EXPECT_TRUE(h->run_until_halt());
    for (int i = 0; i < 20; ++i) h->clock();
    return h;
  };
  const auto masked = run(false);
  EXPECT_EQ(masked->arch().dx, 0);
  EXPECT_EQ(of_type(*masked, BusCycleType::InterruptAck).size(), 0u);

  const auto h = run(true);
  EXPECT_EQ(h->arch().dx, 0x55);
  EXPECT_EQ(h->arch().ax, 25);  // the NOP in the STI shadow ran first
  const auto inta = of_type(*h, BusCycleType::InterruptAck);
  ASSERT_EQ(inta.size(), 2u);
  EXPECT_EQ(inta[1] - inta[0], 1);
  EXPECT_EQ(inta[1]->data & 0xFF, 0x40);
  const auto reads = of_type(*h, BusCycleType::MemRead);
  ASSERT_GE(reads.size(), 2u);
  EXPECT_EQ(reads[0]->addr, 0x100u);
  EXPECT_EQ(reads[1]->addr, 0x102u);
  EXPECT_GT(reads[0]->clk, inta[1]->clk);
}

TEST(Behavioral, HoldFloatsTheBusBetweenCycles) {
  Harness h;
  h.boot({0xB9, 0x14, 0x00,              // mov cx, 20
          0x89, 0x0E, 0x00, 0x02,        // l: mov [0200h], cx
          0x01, 0x0E, 0x02, 0x02,        // add [0202h], cx
          0x49, 0x75, 0xF5});            // dec cx; jnz l
  h.reset();
  for (int i = 0; i < 60; ++i) h.clock();
  h.force["HOLD"] = Logic::L1;
  int waited = 0;
  while (h.level("HLDA") != Logic::L1 && waited < 12) {
    h.clock();
    ++waited;
  }
  ASSERT_EQ(h.level("HLDA"), Logic::L1);
  const TState t = h.dut->bus_state().t;
  EXPECT_TRUE(t == TState::T4 || t == TState::Ti);
  h.clock();
  for (const char* p : {"AD0", "AD15", "A19", "BHE_N", "RD_N", "WR_N", "M_IO_N", "DT_R_N", "DEN_N"}) {
    EXPECT_EQ(h.level(p), Logic::Z) << p;
  }
  EXPECT_EQ(h.level("ALE"), Logic::L0);
  const std::size_t n = h.txns.size();
  for (int i = 0; i < 30; ++i) h.clock();
  EXPECT_EQ(h.txns.size(), n);
  h.force["HOLD"] = Logic::L0;
  ASSERT_TRUE(h.run_until_halt());
  EXPECT_EQ(h.level("HLDA"), Logic::L0);
  EXPECT_EQ(h.mem[0x200] | (h.mem[0x201] << 8), 1);
  EXPECT_EQ(h.mem[0x202] | (h.mem[0x203] << 8), 210);
}

TEST(Behavioral, IntrInterruptsRepAndResumesAtThePrefix) {
  Harness h;
  h.boot({0xC7, 0x06, 0x04, 0x01, 37, 0x00,    // mov [0104h], handler (type 41h)
          0xC7, 0x06, 0x06, 0x01, 0x00, 0x01,  // mov [0106h], 0100h
          0xBE, 0x00, 0x20,                    // mov si, 2000h
          0xBF, 0x00, 0x30,                    // mov di, 3000h
          0xB9, 0x28, 0x00,                    // mov cx, 40
          0xFC, 0xFB,                          // cld; sti
          0xF3, 0xA4,                          // rep movsb  (offset 34)
          0xF4,                                // hlt
          0x8B, 0xEC, 0x8B, 0x56, 0x00,        // handler: mov bp, sp; mov dx, [bp]
          0x43, 0xCF});                        // inc bx; iret
  std::vector<uint8_t> src(40);
  for (std::size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>(i * 3 + 1);
  h.load(0x2000, src);
  h.inta_type = 0x41;
  h.reset();
  auto writes_to_dst = [&] {
    int n = 0;
    for (const Txn* t : of_type(h, BusCycleType::MemWrite)) n += t->addr >= 0x3000 && t->addr < 0x3028;
    return n;
  };
  for (int i = 0; i < 20'000 && writes_to_dst() < 10; ++i) h.clock();
  ASSERT_EQ(writes_to_dst(), 10);
  h.force["INTR"] = Logic::L1;
  for (int i = 0; i < 2'000 && of_type(h, BusCycleType::InterruptAck).size() < 2; ++i) h.clock();
  h.force["INTR"] = Logic::L0;
  const int before = writes_to_dst();
  ASSERT_TRUE(h.run_until_halt());
  EXPECT_EQ(h.arch().bx, 1);
  EXPECT_EQ(h.arch().dx, 34);
  EXPECT_EQ(h.arch().cx, 0);
  EXPECT_GE(before, 10);
  EXPECT_LT(before, 40);
  EXPECT_EQ(writes_to_dst(), 40);
  for (std::size_t i = 0; i < src.size(); ++i) EXPECT_EQ(h.mem[0x3000 + i], src[i]) << i;
}

}  // namespace
}  // namespace i8086::model
