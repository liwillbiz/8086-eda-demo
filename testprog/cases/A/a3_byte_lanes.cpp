// A3 — Byte-lane enables (brief §7 A, §1.5, §3.3): word and byte reads and writes at even
// and odd addresses. BHE#/A0 of every data cycle must decode (spec decode.byte_enable) to
// the expected transfer, and an odd-address word must take two cycles, odd byte first.

#include <algorithm>
#include <format>

#include "protocol.hpp"
#include "rom_case.hpp"

namespace i8086::testprog {
namespace {

struct Expected {
  model::BusCycleType type;
  uint32_t address;
  std::string_view transfer;  // spec decode identifier
};

class A3ByteLanes final : public RomCase {
 public:
  std::string_view id() const override { return "A3"; }
  std::string_view title() const override { return "Byte-lane enables"; }

 protected:
  std::string_view rom_name() const override { return "a3_byte_lanes"; }
  Verdict brief_checks(TestContext& ctx, const RomRun&) override {
    using T = model::BusCycleType;
    static constexpr Expected kCycles[] = {
        {T::MemWrite, 0x02100, "word"},     {T::MemWrite, 0x02111, "high_byte"},
        {T::MemWrite, 0x02112, "low_byte"}, {T::MemWrite, 0x02120, "low_byte"},
        {T::MemWrite, 0x02131, "high_byte"}, {T::MemRead, 0x02100, "word"},
        {T::MemRead, 0x02111, "high_byte"}, {T::MemRead, 0x02112, "low_byte"},
        {T::MemRead, 0x02120, "low_byte"},  {T::MemRead, 0x02131, "high_byte"},
    };
    const auto& trace = ctx.t.last_trace();
    std::vector<const tester::BusTransaction*> data;
    for (const auto& tx : trace.transactions) {
      if ((tx.type == T::MemWrite || tx.type == T::MemRead) && tx.address >= 0x02100 && tx.address < 0x02140) {
        data.push_back(&tx);
      }
    }
    const std::string ref = "brief §3.3, spec decode.byte_enable";
    Verdict v = check(ctx, "data_cycles", static_cast<double>(data.size()), exact(std::size(kCycles), ref));
    const auto& table = ctx.spec.decode().byte_enable;
    for (std::size_t i = 0; i < std::size(kCycles) && i < data.size(); ++i) {
      const auto& want = kCycles[i];
      const auto& got = *data[i];
      const auto code = static_cast<double>((got.bhe_n ? 2 : 0) | (got.address & 1));
      const auto want_code = static_cast<double>(std::find(table.begin(), table.end(), want.transfer) - table.begin());
      const bool same = got.type == want.type && got.address == want.address;
      const std::string param = std::format("cycle{}_bhe_a0", i);
      v = worst(v, check(ctx, param, same ? code : -1, exact(want_code, ref),
                         std::format("{} {}: expected {}, got {}{}", want.type == T::MemWrite ? "write" : "read",
                                     hex(want.address, 5), want.transfer,
                                     table[static_cast<std::size_t>((got.bhe_n ? 2 : 0) | (got.address & 1))],
                                     same ? "" : std::format(" on cycle at {}", hex(got.address, 5)))));
    }
    return v;
  }
};
REGISTER_TEST(A3ByteLanes);

}  // namespace
}  // namespace i8086::testprog
