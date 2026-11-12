#include "interrupts.hpp"

#include <stdexcept>

#include "i8086/golden/rom.hpp"

namespace i8086::testprog {

namespace {
constexpr uint16_t kRomSegment = 0xFF00;  // testprog/roms/inc/i86rom.inc ROM_SEG
}

std::vector<tester::BusTransaction> transactions_of(const tester::BusTrace& trace, model::BusCycleType type) {
  std::vector<tester::BusTransaction> out;
  for (const auto& tx : trace.transactions) {
    if (tx.type == type) out.push_back(tx);
  }
  return out;
}

std::size_t vector_fetches(const tester::BusTrace& trace, uint8_t type) {
  std::size_t n = 0;
  for (const auto& tx : transactions_of(trace, model::BusCycleType::MemRead)) {
    n += tx.address == uint32_t{type} * 4;
  }
  return n;
}

model::Logic level_at(const tester::BusTrace& trace, int pin, uint64_t clk) {
  model::Logic v = model::Logic::X;
  for (const auto& c : trace.cycles) {
    if (c.clk > clk) break;
    for (const auto& e : c.events) {
      if (e.pin == pin) v = e.value;
    }
  }
  return v;
}

std::optional<uint64_t> first_change(const tester::BusTrace& trace, int pin, model::Logic v, uint64_t from) {
  for (const auto& c : trace.cycles) {
    if (c.clk < from) continue;
    for (const auto& e : c.events) {
      if (e.pin == pin && e.value == v) return c.clk;
    }
  }
  return std::nullopt;
}

std::optional<uint64_t> golden_index_at(TestContext& ctx, std::string_view rom_name, uint16_t ip) {
  if (!ctx.golden) throw std::logic_error("golden_index_at: no golden emulator in the test context");
  const auto bytes = rom(rom_name);
  const golden::Program program = golden::rom_program({bytes.begin(), bytes.end()});
  const golden::Result full = ctx.golden->run(program);
  for (uint64_t i = 0; i <= full.instructions; ++i) {
    const golden::Result r = ctx.golden->run(program, i);
    if (r.regs.cs == kRomSegment && r.regs.ip == ip) return i;
  }
  return std::nullopt;
}

}  // namespace i8086::testprog
