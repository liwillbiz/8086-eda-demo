#pragma once

// Layer-private report writers (not part of the frozen interface).

#include <ostream>

#include "i8086/testprog/datalog.hpp"
#include "i8086/testprog/runner.hpp"

namespace i8086::testprog::report {

void write_results_csv(std::ostream&, const RunSummary&);
void write_summary_md(std::ostream&, const RunConfig&, const RunSummary&, const Datalog&);

}  // namespace i8086::testprog::report
