#pragma once

// Protocol checker (design doc §6.1): walks a BusTrace and checks every decoded bus cycle
// against the minimum-mode protocol of brief §4.1 and the decode tables in spec.yaml.

#include <string>
#include <vector>

#include "i8086/spec/spec.hpp"
#include "i8086/tester/tester.hpp"

namespace i8086::testprog {

// Per bus cycle (T1 = start_clk, T4 = T1 + 3 + wait states):
//  - T-state order T1 T2 T3 Tw* T4 in the trace's bus state;
//  - one ALE pulse inside T1;
//  - its strobe (RD#, WR# or INTA#) falls in T2 and rises in T4, and no other strobe moves;
//  - DEN# active from T1/T2 to T4; DT/R# and M/IO# match the cycle type;
//  - S6 = 0, a code fetch reports segment CS_or_none, BHE#/A0 is not "none".
// Returns one message per violation (at most `max_messages`, then a count).
std::vector<std::string> check_protocol(const tester::BusTrace&, const spec::Spec&,
                                        std::size_t max_messages = 8);

// Bus status pins S6..S3 (A19..A16) and BHE# as resolved during a cycle's strobe.
struct CycleStatus {
  int s3 = -1, s4 = -1, s5 = -1, s6 = -1;  // -1 = not 0/1
  std::string segment;                     // spec decode identifier, e.g. "SS"
  std::string transfer;                    // byte_enable identifier, e.g. "high_byte"
};
CycleStatus cycle_status(const tester::BusTrace&, const spec::Spec&, const tester::BusTransaction&);

}  // namespace i8086::testprog
