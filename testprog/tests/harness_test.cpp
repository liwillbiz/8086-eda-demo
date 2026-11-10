// Layer-private harness: observation from golden results and bus traces, the masked diff,
// the protocol checker, and the embedded ROMs.

#include <gtest/gtest.h>

#include "embedded.hpp"
#include "functional.hpp"
#include "i8086/golden/rom.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/tester/sim_tester.hpp"
#include "protocol.hpp"

namespace i8086::testprog {
namespace {

using model::BusCycleType;

Observation flags_report(uint16_t flags) {
  Observation o;
  o.io = {{kPortFlags, static_cast<uint8_t>(flags & 0xFF)},
          {kPortFlags + 1, static_cast<uint8_t>(flags >> 8)}};
  o.halted = true;
  return o;
}

TEST(Observe, GoldenWordOutSplitsIntoBytesLowFirst) {
  golden::Result r;
  r.io_writes = {{kPortData, 0x1234, true}, {0x41, 0x56, false}};
  r.mem_writes = {{0x2000, 0xAB}};
  const Observation o = observe(r);
  ASSERT_EQ(o.io.size(), 3u);
  EXPECT_EQ(o.io[0], (IoByte{kPortData, 0x34}));
  EXPECT_EQ(o.io[1], (IoByte{kPortData + 1, 0x12}));
  EXPECT_EQ(o.io[2], (IoByte{0x41, 0x56}));
  EXPECT_EQ(o.data(), std::vector<uint16_t>{0x1234});
  EXPECT_EQ(o.mem, (std::vector<MemByte>{{0x2000, 0xAB}}));
}

TEST(Observe, BusCyclesMapToByteLanes) {
  tester::BusTrace trace;
  trace.transactions = {
      {BusCycleType::MemWrite, 0x02100, 0x1234, false, 0, 0},  // even word
      {BusCycleType::MemWrite, 0x02111, 0x7800, false, 0, 0},  // odd byte (high lane)
      {BusCycleType::MemWrite, 0x02112, 0x0056, true, 0, 0},   // even byte (low lane)
      {BusCycleType::MemRead, 0x02100, 0x1234, false, 0, 0},   // reads are not observed
      {BusCycleType::IoWrite, kPortData, 0xBEEF, false, 0, 0},
  };
  const Observation o = observe(trace, true);
  EXPECT_EQ(o.mem, (std::vector<MemByte>{{0x02100, 0x34}, {0x02101, 0x12}, {0x02111, 0x78}, {0x02112, 0x56}}));
  EXPECT_EQ(o.data(), std::vector<uint16_t>{0xBEEF});
}

TEST(Diff, UndefinedFlagsAreMaskedAndDefinedOnesAreNot) {
  DiffOptions opt;
  opt.undefined_flags = flag::AF;
  EXPECT_TRUE(diff(flags_report(0xF002), flags_report(0xF012), opt).empty());
  const auto d = diff(flags_report(0xF002), flags_report(0xF003), opt);
  ASSERT_EQ(d.size(), 1u);
  EXPECT_NE(d[0].find("00E2h"), std::string::npos) << d[0];
  opt.undefined_flags = flag::OF;  // high byte
  EXPECT_TRUE(diff(flags_report(0xF002), flags_report(0xF802), opt).empty());
}

TEST(Diff, PerReportMaskOverridesTheDefault) {
  Observation g = flags_report(0x0000), d = flags_report(0x0010);
  g.io.insert(g.io.end(), {{kPortFlags, 0x00}, {kPortFlags + 1, 0x00}});
  d.io.insert(d.io.end(), {{kPortFlags, 0x10}, {kPortFlags + 1, 0x00}});
  DiffOptions opt;
  opt.per_report = {flag::AF, 0};
  const auto out = diff(g, d, opt);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_NE(out[0].find("io[2]"), std::string::npos) << out[0];
}

TEST(Diff, DataPortIsExactAndCountsMatter) {
  Observation g, d;
  g.io = {{kPortData, 1}, {kPortData + 1, 0}};
  d.io = {{kPortData, 1}, {kPortData + 1, 0}, {kPortData, 2}, {kPortData + 1, 0}};
  EXPECT_FALSE(diff(g, d, {}).empty());
  d.io.resize(2);
  d.io[0].value = 3;
  EXPECT_FALSE(diff(g, d, {}).empty());
}

TEST(Diff, StackBytesCompareByCountOnly) {
  Observation g, d;
  g.mem = {{0x0FFE, 0x12}, {0x2000, 0x01}};
  d.mem = {{0x0FFE, 0x99}, {0x2000, 0x01}};
  EXPECT_TRUE(diff(g, d, {}).empty());
  d.mem.push_back({0x0FFC, 0x00});
  EXPECT_FALSE(diff(g, d, {}).empty());
  d.mem = {{0x0FFE, 0x12}, {0x2000, 0x02}};
  EXPECT_FALSE(diff(g, d, {}).empty());
}

TEST(Diff, HaltMismatchIsReported) {
  Observation g, d;
  g.halted = true;
  EXPECT_FALSE(diff(g, d, {}).empty());
}

TEST(Assets, EveryRomIsAFourKilobyteImageWithAResetJump) {
  ASSERT_TRUE(embedded::nasm_found()) << "configure with NASM installed (decision D7)";
  ASSERT_FALSE(embedded::roms().empty());
  for (const auto& r : embedded::roms()) {
    ASSERT_EQ(r.bytes.size(), 0x1000u) << r.name;
    EXPECT_EQ(r.bytes[0xFF0], 0xEA) << r.name << ": no far JMP at FFFF0h";
  }
  EXPECT_THROW(rom("no_such_rom"), std::invalid_argument);
  EXPECT_NO_THROW(pattern("reset_vector"));
}

class Protocol : public testing::Test {
 protected:
  spec::Spec spec = spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml");
  model::DieProfile die = model::DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");

  tester::BusTrace run(std::string_view name) {
    tester::SimTester t(spec, model::make_behavioral(spec, die, {}), tester::ElectricalModel(die));
    t.set_timing(0, functional_timing(spec, spec::Grade::G8086_1));
    t.power(true);
    tester::MemoryImage image;
    const auto bytes = rom(name);
    image.load(kRomTop - static_cast<uint32_t>(bytes.size()), bytes);
    t.run_program(image, {});
    return t.last_trace();
  }
};

TEST_F(Protocol, CleanTraceHasNoViolations) {
  const auto trace = run("a3_byte_lanes");
  ASSERT_GT(trace.transactions.size(), 20u);
  EXPECT_EQ(check_protocol(trace, spec), std::vector<std::string>{});
}

TEST_F(Protocol, MissingAleAndLateStrobeAreCaught) {
  auto trace = run("boot");
  const auto& first = trace.transactions.front();
  const int ale = spec.pin("ALE").num, rd = spec.pin("RD_N").num;
  auto& t1 = trace.cycles[first.start_clk].events;
  std::erase_if(t1, [&](const model::PinEvent& e) { return e.pin == ale; });
  auto& t2 = trace.cycles[first.start_clk + 1].events;
  auto& t3 = trace.cycles[first.start_clk + 2].events;
  for (auto it = t2.begin(); it != t2.end(); ++it) {
    if (it->pin == rd) {
      t3.insert(t3.begin(), *it);
      t2.erase(it);
      break;
    }
  }
  const auto v = check_protocol(trace, spec);
  ASSERT_GE(v.size(), 2u);
  EXPECT_NE(v[0].find("ALE"), std::string::npos) << v[0];
  EXPECT_NE(v[1].find("active in T2"), std::string::npos) << v[1];
}

TEST_F(Protocol, CodeFetchStatusIsCsOrNone) {
  const auto trace = run("boot");
  const auto s = cycle_status(trace, spec, trace.transactions.front());
  EXPECT_EQ(s.segment, "CS_or_none");
  EXPECT_EQ(s.s6, 0);
  EXPECT_EQ(s.transfer, "word");
}

}  // namespace
}  // namespace i8086::testprog
