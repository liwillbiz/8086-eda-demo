// T-state sequences, pin timing, byte lanes, prefetch queue, RESET (design doc §8 model tests).

#include <gtest/gtest.h>

#include <algorithm>

#include "harness.hpp"

namespace i8086::model::test {
namespace {

int64_t ps(const DieProfile& die, const char* sym) {
  return static_cast<int64_t>(die.timing_ns.at(sym) * 1000.0);
}

const PinEvent* find_event(const EdgeLog& e, int pin) {
  for (const PinEvent& p : e.events) {
    if (p.pin == pin) return &p;
  }
  return nullptr;
}

std::vector<Txn> of_type(const std::vector<Txn>& txns, BusCycleType t) {
  std::vector<Txn> out;
  std::copy_if(txns.begin(), txns.end(), std::back_inserter(out), [&](const Txn& x) { return x.type == t; });
  return out;
}

TEST(Reset, FloatsOutputsAndLoadsResetState) {
  Harness h;
  h.boot({});
  h.reset(4);
  EXPECT_EQ(h.level("AD0"), Logic::Z);
  EXPECT_EQ(h.level("A19"), Logic::Z);
  EXPECT_EQ(h.level("RD_N"), Logic::Z);
  EXPECT_EQ(h.level("WR_N"), Logic::Z);
  EXPECT_EQ(h.level("BHE_N"), Logic::Z);
  EXPECT_EQ(h.level("ALE"), Logic::L0);
  EXPECT_EQ(h.level("INTA_N"), Logic::L1);
  const ArchState a = h.arch();
  EXPECT_EQ(a.cs, 0xFFFF);
  EXPECT_EQ(a.ip, 0x0000);
  EXPECT_EQ(a.ds, 0);
  EXPECT_EQ(a.ss, 0);
  EXPECT_EQ(a.es, 0);
  EXPECT_EQ(a.flags, 0xF002);  // all defined flags clear; reserved bits as the 8086 reads them
  EXPECT_EQ(a.queue_len, 0);
}

TEST(Reset, FirstFetchAtFFFF0AboutSevenClocksAfterReset) {
  Harness h;
  h.boot({});
  h.reset(4);
  const uint64_t released = h.dut->bus_state().clk;  // RESET dropped before the next falling edge
  while (h.txns.empty()) h.clock();
  const Txn& t = h.txns.front();
  EXPECT_EQ(t.type, BusCycleType::CodeFetch);
  EXPECT_EQ(t.addr, 0xFFFF0u);
  EXPECT_FALSE(t.bhe_n);
  EXPECT_EQ(t.data, 0x00EA);
  EXPECT_EQ(t.clk - released, 7u);
}

TEST(Reset, ResetMidProgramRestarts) {
  Harness h;
  h.boot({0xEB, 0xFE});  // JMP $
  h.reset();
  for (int i = 0; i < 200; ++i) h.clock();
  EXPECT_EQ(h.arch().cs, 0x0100);
  h.txns.clear();
  h.reset();
  while (h.txns.empty()) h.clock();
  EXPECT_EQ(h.txns.front().addr, 0xFFFF0u);
}

TEST(BusCycle, FetchRunsT1ToT4WithStrobesAtDieDelays) {
  Harness h;
  h.boot({});
  h.reset();
  // Walk until the first T1.
  std::size_t i = h.log.size();
  while (h.dut->bus_state().t != TState::T1) h.clock();
  for (; i < h.log.size(); ++i) {
    if (h.log[i].edge == Edge::Falling && h.log[i].bus.t == TState::T1) break;
  }
  for (int n = 0; n < 8; ++n) h.clock();
  ASSERT_LT(i + 7, h.log.size());
  const EdgeLog& t1f = h.log[i];
  const EdgeLog& t1r = h.log[i + 1];
  const EdgeLog& t2f = h.log[i + 2];
  const EdgeLog& t3f = h.log[i + 4];
  const EdgeLog& t4f = h.log[i + 6];
  EXPECT_EQ(t1f.bus.t, TState::T1);
  EXPECT_EQ(t2f.bus.t, TState::T2);
  EXPECT_EQ(t3f.bus.t, TState::T3);
  EXPECT_EQ(t4f.bus.t, TState::T4);
  EXPECT_EQ(t1f.bus.cycle, BusCycleType::CodeFetch);
  EXPECT_EQ(t1f.bus.address, 0xFFFF0u);

  const PinEvent* ale_up = find_event(t1f, h.pin("ALE"));
  ASSERT_NE(ale_up, nullptr);
  EXPECT_EQ(ale_up->value, Logic::L1);
  EXPECT_EQ(ale_up->delay_ps, ps(h.die, "TCLLH"));
  const PinEvent* ad4 = find_event(t1f, h.pin("AD4"));  // FFFF0h: AD4 = 1
  ASSERT_NE(ad4, nullptr);
  EXPECT_EQ(ad4->value, Logic::L1);
  EXPECT_EQ(ad4->delay_ps, ps(h.die, "TCLAV"));
  const PinEvent* ale_dn = find_event(t1r, h.pin("ALE"));
  ASSERT_NE(ale_dn, nullptr);
  EXPECT_EQ(ale_dn->value, Logic::L0);
  EXPECT_EQ(ale_dn->delay_ps, ps(h.die, "TCHLL"));

  const PinEvent* rd_lo = find_event(t2f, h.pin("RD_N"));
  ASSERT_NE(rd_lo, nullptr);
  EXPECT_EQ(rd_lo->value, Logic::L0);
  EXPECT_EQ(rd_lo->delay_ps, ps(h.die, "TCLRL"));
  const PinEvent* ad_z = find_event(t2f, h.pin("AD4"));
  ASSERT_NE(ad_z, nullptr);
  EXPECT_EQ(ad_z->value, Logic::Z);
  EXPECT_EQ(ad_z->delay_ps, ps(h.die, "TCLAZ"));
  const PinEvent* den = find_event(t2f, h.pin("DEN_N"));
  ASSERT_NE(den, nullptr);
  EXPECT_EQ(den->value, Logic::L0);

  const PinEvent* rd_hi = find_event(t4f, h.pin("RD_N"));
  ASSERT_NE(rd_hi, nullptr);
  EXPECT_EQ(rd_hi->value, Logic::L1);
  EXPECT_EQ(rd_hi->delay_ps, ps(h.die, "TCLRH"));
  EXPECT_EQ(find_event(t4f, h.pin("DEN_N"))->value, Logic::L1);
  EXPECT_EQ(h.level("M_IO_N"), Logic::L1);
}

TEST(BusCycle, StatusAppearsOnA19toA16InT2) {
  Harness h;
  h.boot({});
  h.reset();
  while (h.dut->bus_state().t != TState::T2) h.clock();
  // Code fetch: S4 S3 = 10 (CS or none), S5 = IF = 0, S6 = 0.
  EXPECT_EQ(h.level("A16"), Logic::L0);
  EXPECT_EQ(h.level("A17"), Logic::L1);
  EXPECT_EQ(h.level("A18"), Logic::L0);
  EXPECT_EQ(h.level("A19"), Logic::L0);
}

TEST(BusCycle, WordWriteDrivesDataAndWrStrobe) {
  Harness h;
  // MOV AX,1234h ; MOV [0100h],AX
  h.boot({0xB8, 0x34, 0x12, 0xA3, 0x00, 0x01});
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  const auto w = of_type(h.txns, BusCycleType::MemWrite);
  ASSERT_EQ(w.size(), 1u);
  EXPECT_EQ(w[0].addr, 0x00100u);
  EXPECT_FALSE(w[0].bhe_n);
  EXPECT_EQ(w[0].data, 0x1234);
  EXPECT_EQ(w[0].s4s3, 0b11);  // DS

  // WR# pulses low from T2 to T4 with DT/R# high (transmit); RD# stays high.
  bool saw_wr_low = false;
  for (const EdgeLog& e : h.log) {
    if (e.bus.cycle != BusCycleType::MemWrite) continue;
    if (const PinEvent* p = find_event(e, h.pin("WR_N"))) {
      if (p->value == Logic::L0) {
        saw_wr_low = true;
        EXPECT_EQ(e.bus.t, TState::T2);
        EXPECT_EQ(p->delay_ps, ps(h.die, "TCVCTV"));
      } else {
        EXPECT_EQ(e.bus.t, TState::T4);
      }
    }
    EXPECT_EQ(find_event(e, h.pin("RD_N")), nullptr);
  }
  EXPECT_TRUE(saw_wr_low);
}

TEST(BusCycle, ByteLanesFollowBheA0Table) {
  Harness h;
  // MOV AX,BBAAh ; MOV [0200h],AL ; MOV [0201h],AH ; MOV [0202h],AX ; MOV [0205h],AX
  h.boot({0xB8, 0xAA, 0xBB, 0xA2, 0x00, 0x02, 0x88, 0x26, 0x01, 0x02, 0xA3, 0x02, 0x02, 0xA3,
          0x05, 0x02});
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  const auto w = of_type(h.txns, BusCycleType::MemWrite);
  ASSERT_EQ(w.size(), 5u);  // the odd-address word splits into two cycles
  EXPECT_EQ(w[0].addr, 0x200u);
  EXPECT_TRUE(w[0].bhe_n);  // low byte, even address
  EXPECT_EQ(w[0].data, 0xAA);
  EXPECT_EQ(w[1].addr, 0x201u);
  EXPECT_FALSE(w[1].bhe_n);  // high byte, odd address
  EXPECT_EQ(w[1].data, 0xBB);
  EXPECT_EQ(w[2].addr, 0x202u);
  EXPECT_FALSE(w[2].bhe_n);  // whole word
  EXPECT_EQ(w[2].data, 0xBBAA);
  EXPECT_EQ(w[3].addr, 0x205u);
  EXPECT_FALSE(w[3].bhe_n);
  EXPECT_EQ(w[3].data, 0xAA);
  EXPECT_EQ(w[4].addr, 0x206u);
  EXPECT_TRUE(w[4].bhe_n);
  EXPECT_EQ(w[4].data, 0xBB);
}

TEST(BusCycle, OddWordReadTakesTwoCycles) {
  Harness h;
  h.load(0x301, {0x34, 0x12});
  h.boot({0xA1, 0x01, 0x03});  // MOV AX,[0301h]
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  const auto r = of_type(h.txns, BusCycleType::MemRead);
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0].addr, 0x301u);
  EXPECT_FALSE(r[0].bhe_n);
  EXPECT_EQ(r[1].addr, 0x302u);
  EXPECT_TRUE(r[1].bhe_n);
  EXPECT_EQ(h.arch().ax, 0x1234);
}

TEST(BusCycle, ReadyLowInsertsExactlyNWaitStates) {
  for (int n : {0, 1, 3}) {
    Harness h;
    h.wait_states = n;
    h.boot({0xA1, 0x00, 0x03});
    h.reset();
    ASSERT_TRUE(h.run_until_halt());
    ASSERT_FALSE(h.txns.empty());
    for (const Txn& t : h.txns) {
      if (t.type != BusCycleType::Halt) {
        EXPECT_EQ(t.waits, n) << "cycle at " << t.addr;
      }
    }
  }
}

TEST(BusCycle, IoCyclesDriveMIoLowAndUsePortAddress) {
  Harness h;
  h.io_in[0x41] = 0x34;
  h.io_in[0x42] = 0x12;
  // MOV AL,5Ah ; OUT 80h,AL ; MOV DX,0041h ; IN AX,DX
  h.boot({0xB0, 0x5A, 0xE6, 0x80, 0xBA, 0x41, 0x00, 0xED});
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  const auto out = of_type(h.txns, BusCycleType::IoWrite);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].addr, 0x80u);
  EXPECT_EQ(out[0].data, 0x5A);
  const auto in = of_type(h.txns, BusCycleType::IoRead);
  ASSERT_EQ(in.size(), 2u);  // odd-port word: two byte cycles
  EXPECT_EQ(in[0].addr, 0x41u);
  EXPECT_EQ(in[1].addr, 0x42u);
  EXPECT_EQ(h.arch().ax, 0x1234);
  Logic mio = Logic::X;
  int io_states = 0;
  for (const EdgeLog& e : h.log) {
    if (const PinEvent* p = find_event(e, h.pin("M_IO_N"))) mio = p->value;
    const bool io = e.bus.cycle == BusCycleType::IoRead || e.bus.cycle == BusCycleType::IoWrite;
    if (e.edge == Edge::Falling && e.bus.t == TState::T2) {
      EXPECT_EQ(mio, io ? Logic::L0 : Logic::L1);
      io_states += io ? 1 : 0;
    }
  }
  EXPECT_EQ(io_states, 3);
}

TEST(BusCycle, HaltRunsAnAlePulseWithoutStrobes) {
  Harness h;
  h.boot({});
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  const auto halts = of_type(h.txns, BusCycleType::Halt);
  ASSERT_EQ(halts.size(), 1u);
  EXPECT_TRUE(h.arch().halted);
  const std::size_t after = h.txns.size();
  for (int i = 0; i < 50; ++i) h.clock();
  EXPECT_EQ(h.txns.size(), after);  // no more bus cycles while halted
  EXPECT_EQ(h.dut->bus_state().t, TState::Ti);
  EXPECT_EQ(h.level("RD_N"), Logic::L1);
  EXPECT_EQ(h.level("WR_N"), Logic::L1);
}

TEST(Queue, FillsToSixBytesThenIdles) {
  Harness h;
  // NOP ; MOV DX,0 ; MOV BX,3 ; DIV BX (long EU time) ; NOP x8. Word prefetches start only with
  // 2 bytes free, so the queue fills to 6 only when the EU has consumed an even byte count.
  h.boot({0x90, 0xBA, 0x00, 0x00, 0xBB, 0x03, 0x00, 0xF7, 0xF3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
          0x90, 0x90});
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  const uint64_t first = h.txns.front().clk;
  const uint64_t halt = of_type(h.txns, BusCycleType::Halt).at(0).clk;
  uint8_t max_q = 0;
  bool idle_with_full_queue = false;
  for (std::size_t i = 1; i < h.log.size(); ++i) {
    const EdgeLog& e = h.log[i];
    const EdgeLog& decided = h.log[i - 1];  // the rising edge where the BIU chose "no cycle"
    max_q = std::max(max_q, e.bus.queue_bytes);
    if (e.edge == Edge::Falling && e.bus.t == TState::Ti && decided.bus.clk > first &&
        decided.bus.clk < halt) {
      EXPECT_GE(decided.bus.queue_bytes, 5) << "clk " << decided.bus.clk;
      if (decided.bus.queue_bytes == 6) idle_with_full_queue = true;
    }
    // A fetch starts only when at least 2 bytes are free.
    if (e.edge == Edge::Falling && e.bus.t == TState::T1 && e.bus.cycle == BusCycleType::CodeFetch) {
      EXPECT_LE(e.bus.queue_bytes, 4);
    }
  }
  EXPECT_EQ(max_q, 6);
  EXPECT_TRUE(idle_with_full_queue);
}

TEST(Queue, JumpFlushesAndRefetchesFromTarget) {
  Harness h;
  // JMP short +4 to an odd offset; the skipped HLTs are fetched but never executed.
  h.boot({0xEB, 0x04, 0xF4, 0xF4, 0xF4, 0xF4, 0xB8, 0x77, 0x66});
  h.reset();
  ASSERT_TRUE(h.run_until_halt());
  EXPECT_EQ(h.arch().ax, 0x6677);
  // After the jump, the first fetch is a byte fetch at the odd target address.
  const uint32_t target = Harness::kCodeBase + Harness::kPrologueLen + 2 + 4;
  auto it = std::find_if(h.txns.begin(), h.txns.end(),
                         [&](const Txn& t) { return t.type == BusCycleType::CodeFetch && t.addr == target; });
  ASSERT_NE(it, h.txns.end());
  EXPECT_FALSE(it->bhe_n);
  EXPECT_EQ(it->data, 0xB8);
  ASSERT_NE(it + 1, h.txns.end());
  EXPECT_EQ((it + 1)->addr, target + 1);  // then word fetches from the even address
  EXPECT_FALSE((it + 1)->bhe_n);
}

}  // namespace
}  // namespace i8086::model::test
