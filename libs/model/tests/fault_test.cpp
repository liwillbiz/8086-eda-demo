// Fault injection (design doc §4.2, M7): die-file parsing and every FaultInjector hook.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "harness.hpp"
#include "i8086/model/fault_injector.hpp"

namespace i8086::model::test {
namespace {

int64_t ps(const Harness& h, const char* sym) { return static_cast<int64_t>(h.die.timing_ns.at(sym) * 1000.0); }

std::vector<const PinEvent*> events_on(const Harness& h, const std::string& pin) {
  std::vector<const PinEvent*> out;
  for (const EdgeLog& e : h.log) {
    for (const PinEvent& p : e.events) {
      if (p.pin == h.pin(pin)) out.push_back(&p);
    }
  }
  return out;
}

std::array<uint8_t, 2> first_fetch_bytes(Harness& h) {
  h.boot({});
  h.reset();
  while (h.txns.empty()) h.clock();
  const ArchState a = h.arch();
  return {a.queue[0], a.queue[1]};
}

// mov ax,1234h ; mov [10h],ax ; hlt — returns the word the device wrote.
uint16_t written_word(Harness& h) {
  h.boot({0xB8, 0x34, 0x12, 0xA3, 0x10, 0x00, 0xF4});
  h.reset();
  EXPECT_TRUE(h.run_until_halt());
  for (const Txn& t : h.txns) {
    if (t.type == BusCycleType::MemWrite) return t.data;
  }
  ADD_FAILURE() << "no write";
  return 0;
}

std::filesystem::path write_die(const std::string& faults) {
  std::ifstream in(I86_SOURCE_DIR "/spec/dies/good.yaml");
  std::string text((std::istreambuf_iterator<char>(in)), {});
  text.replace(text.find("faults: []"), 10, "faults:\n" + faults);
  const auto path = std::filesystem::temp_directory_path() / "i86_fault_test_die.yaml";
  std::ofstream(path) << text;
  return path;
}

// ---- die files

TEST(FaultParsing, ReadsEveryFaultType) {
  const DieProfile die = DieProfile::load(write_die(
      "  - { type: StuckAt, pin: AD3, value: 1, start_clk: 40 }\n"
      "  - { type: SlowTiming, params: [TCLRL, TCLRH], scale: 1.5, add_ns: 2 }\n"
      "  - { type: SlowTiming, params: all, scale: 2 }\n"
      "  - { type: SetupDegrade, input: ready, add_ns: 25 }\n"
      "  - { type: LowVccFail, vcc_floor_v: 4.75 }\n"
      "  - { type: Leaky, pin: READY, leakage_ua: 25 }\n"
      "  - { type: LogicBug, name: inc_clears_cf }\n"));
  ASSERT_EQ(die.faults.faults.size(), 7u);
  const StuckAt& s = *die.faults.all<StuckAt>().at(0);
  EXPECT_EQ(s.pin, "AD3");
  EXPECT_EQ(s.value, 1);
  EXPECT_EQ(s.start_clk, 40u);
  const auto slow = die.faults.all<SlowTiming>();
  EXPECT_EQ(slow.at(0)->params, (std::vector<std::string>{"TCLRL", "TCLRH"}));
  EXPECT_DOUBLE_EQ(slow.at(0)->scale, 1.5);
  EXPECT_DOUBLE_EQ(slow.at(0)->add_ns, 2);
  EXPECT_TRUE(slow.at(1)->params.empty());
  EXPECT_EQ(die.faults.all<SetupDegrade>().at(0)->input, InputClass::Ready);
  EXPECT_DOUBLE_EQ(die.faults.all<LowVccFail>().at(0)->vcc_floor_v, 4.75);
  EXPECT_DOUBLE_EQ(die.faults.all<Leaky>().at(0)->leakage_ua, 25);
  EXPECT_TRUE(die.faults.has_logic_bug("inc_clears_cf"));
}

TEST(FaultParsing, BadEntriesAreSpecErrorsWithALine) {
  for (const char* bad : {"  - { type: Melted }\n", "  - { type: StuckAt, pin: AD3, value: 2 }\n",
                          "  - { type: StuckAt, value: 1 }\n", "  - { type: SetupDegrade, input: clk, add_ns: 1 }\n",
                          "  - { pin: AD3 }\n"}) {
    try {
      DieProfile::load(write_die(bad));
      ADD_FAILURE() << "accepted " << bad;
    } catch (const spec::SpecError& e) {
      EXPECT_NE(std::string(e.what()).find("fault"), std::string::npos) << e.what();
      EXPECT_GT(e.line(), 0) << bad;
    }
  }
}

TEST(FaultParsing, EveryFaultDieLoadsWithOneFault) {
  int n = 0;
  for (const auto& entry : std::filesystem::directory_iterator(I86_SOURCE_DIR "/spec/dies")) {
    if (!entry.path().filename().string().starts_with("fault_")) continue;
    const DieProfile die = DieProfile::load(entry.path());
    EXPECT_EQ(die.name, entry.path().stem().string());
    EXPECT_EQ(die.faults.faults.size(), 1u) << die.name;
    Harness h(die.faults);  // the injector validates pins and parameters
    ++n;
  }
  EXPECT_GE(n, 8);
}

TEST(FaultInjector, RejectsUnknownPinsAndParameters) {
  EXPECT_THROW(Harness(FaultSet{{StuckAt{"AD99", 1, std::nullopt}}}), spec::SpecError);
  EXPECT_THROW(Harness(FaultSet{{StuckAt{"VCC", 1, std::nullopt}}}), spec::SpecError);
  EXPECT_THROW(Harness(FaultSet{{SlowTiming{{"TCLRX"}, 1.0, 5.0}}}), spec::SpecError);
}

// ---- StuckAt

TEST(StuckAt, DataPinReadsAndDrivesTheStuckValue) {
  Harness h(FaultSet{{StuckAt{"AD3", 1, std::nullopt}}});
  h.load(0xFFFF8, {0x12, 0x34});
  // Memory sees FFFF8h (address bit 3 stuck), and the core reads bit 3 of the low byte as 1.
  EXPECT_EQ(first_fetch_bytes(h), (std::array<uint8_t, 2>{0x1A, 0x34}));
  EXPECT_EQ(h.txns.front().addr, 0xFFFF8u);
  bool saw_z = false;
  for (const PinEvent* e : events_on(h, "AD3")) {
    EXPECT_NE(e->value, Logic::L0);
    saw_z |= e->value == Logic::Z;
  }
  EXPECT_TRUE(saw_z) << "a 3-state pin still floats when the device releases it";
}

TEST(StuckAt, StartsAtTheGivenClock) {
  Harness h(FaultSet{{StuckAt{"ALE", 0, 60}}});
  h.boot({0xEB, 0xFE});  // jmp $
  h.reset();
  for (int i = 0; i < 120; ++i) h.clock();
  bool high_before = false, low_at_start = false, high_after = false;
  uint64_t clk = 0;
  for (const EdgeLog& e : h.log) {
    if (e.edge == Edge::Rising) ++clk;
    for (const PinEvent& p : e.events) {
      if (p.pin != h.pin("ALE")) continue;
      if (clk < 60 && p.value == Logic::L1) high_before = true;
      if (clk == 60 && p.value == Logic::L0) low_at_start = true;
      if (clk > 60 && p.value == Logic::L1) high_after = true;
    }
  }
  EXPECT_TRUE(high_before);
  EXPECT_TRUE(low_at_start);
  EXPECT_FALSE(high_after);
}

// ---- SlowTiming

TEST(SlowTiming, DelaysOnlyTheNamedEdges) {
  Harness h(FaultSet{{SlowTiming{{"TCLRL"}, 1.0, 30.0}}});
  first_fetch_bytes(h);
  for (int i = 0; i < 4; ++i) h.clock();
  bool fall = false, rise = false;
  for (const EdgeLog& log : h.log) {
    if (log.edge != Edge::Falling) continue;  // rising edges only release RESET (TCVCTX)
    for (const PinEvent& e : log.events) {
      if (e.pin != h.pin("RD_N")) continue;
      if (e.value == Logic::L0) {
        fall = true;
        EXPECT_EQ(e.delay_ps, ps(h, "TCLRL") + 30'000);
      } else if (e.value == Logic::L1) {
        rise = true;
        EXPECT_EQ(e.delay_ps, ps(h, "TCLRH"));
      }
    }
  }
  EXPECT_TRUE(fall);
  EXPECT_TRUE(rise);
}

TEST(SlowTiming, AllScalesAddressAndStrobeDelays) {
  Harness h(FaultSet{{SlowTiming{{}, 2.0, 0.0}}});
  first_fetch_bytes(h);
  for (const PinEvent* e : events_on(h, "ALE")) {
    if (e->value == Logic::L1 && e->delay_ps != 0) {
      EXPECT_EQ(e->delay_ps, 2 * ps(h, "TCLLH"));
    }
  }
  for (const PinEvent* e : events_on(h, "A19")) {
    if (e->value != Logic::Z && e->delay_ps != 0) {
      EXPECT_EQ(e->delay_ps, 2 * ps(h, "TCLAV"));
    }
  }
}

TEST(SlowTiming, CriticalPathSlowsWriteDataPastTheClock) {
  Harness ok;
  EXPECT_EQ(written_word(ok), 0x1234);
  Harness slow(FaultSet{{SlowTiming{{"TCLCL_CRIT"}, 4.0, 0.0}}});  // 62 ns x4 > 200 ns
  EXPECT_NE(written_word(slow), 0x1234);
}

// ---- SetupDegrade

TEST(SetupDegrade, DataNeedsMoreSetup) {
  const auto at = [](FaultSet f) {
    Harness h(std::move(f));
    h.data_changed_ps = -ps(h, "TDVCL") - 1000;  // 1 ns inside the good die's window
    return first_fetch_bytes(h);
  };
  EXPECT_EQ(at({}), (std::array<uint8_t, 2>{0xEA, 0x00}));
  EXPECT_NE(at(FaultSet{{SetupDegrade{InputClass::Data, 2.0}}}), (std::array<uint8_t, 2>{0xEA, 0x00}));
}

TEST(SetupDegrade, ReadyNeedsMoreSetup) {
  const auto waits = [](FaultSet f) {
    Harness h(std::move(f));
    h.wait_states = 1;
    h.ready_changed_ps = -ps(h, "TRYHCH") - 1000;
    first_fetch_bytes(h);
    return h.txns.front().waits;
  };
  EXPECT_EQ(waits({}), 1);
  EXPECT_EQ(waits(FaultSet{{SetupDegrade{InputClass::Ready, 2.0}}}), 2);
}

// ---- LowVccFail

TEST(LowVccFail, CorruptsWriteDataOnlyBelowTheFloor) {
  const auto at = [](double vcc) {
    Harness h(FaultSet{{LowVccFail{4.75}}});
    h.dut->set_conditions({vcc, 25.0, h.tclcl_ns});
    return written_word(h);
  };
  EXPECT_EQ(at(5.0), 0x1234);
  EXPECT_EQ(at(4.75), 0x1234);
  EXPECT_EQ(at(4.5), 0x1335);  // bit 0 of each byte lane inverted
}

}  // namespace
}  // namespace i8086::model::test
