#pragma once

// Layer-private helpers for the interrupt, HOLD and HLT tests (A4-A7, B6, D7, D10): bus
// trace queries and the golden instruction index that matches a DUT stimulus.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "functional.hpp"

namespace i8086::testprog {

std::vector<tester::BusTransaction> transactions_of(const tester::BusTrace&, model::BusCycleType);

// Memory reads of the 4-byte vector of `type`, i.e. interrupt entries seen on the bus.
std::size_t vector_fetches(const tester::BusTrace&, uint8_t type);

// The DUT's level on `pin` at the end of CLK `clk`, from the trace's pin events.
model::Logic level_at(const tester::BusTrace&, int pin, uint64_t clk);

// The first CLK at or after `from` in which the DUT drives `pin` to `v`.
std::optional<uint64_t> first_change(const tester::BusTrace&, int pin, model::Logic v, uint64_t from = 0);

// The index of the first instruction the golden emulator starts at CS:`ip` (CS = the ROM
// segment) when running `rom_name` without interrupts, for an InterruptRequest that the
// golden takes just before that instruction.
std::optional<uint64_t> golden_index_at(TestContext&, std::string_view rom_name, uint16_t ip);

}  // namespace i8086::testprog
