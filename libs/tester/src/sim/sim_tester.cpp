#include "i8086/tester/sim_tester.hpp"

#include "i8086/common/errors.hpp"

namespace i8086::tester {

SimTester::SimTester(const spec::Spec& spec, std::unique_ptr<model::DeviceModel> dut,
                     ElectricalModel electrical)
    : spec_(spec), dut_(std::move(dut)), electrical_(std::move(electrical)) {}

// M0 stub: stores configuration; abs_max guard and execution land in M4.
void SimTester::set_levels(const LevelSet& levels) { levels_ = levels; }
void SimTester::set_timing(int id, const TimingSet& ts) { timings_[id] = ts; }
void SimTester::set_temperature(double temp_c) { temp_c_ = temp_c; }
void SimTester::power(bool on) { powered_ = on; }

FunctionalResult SimTester::run_pattern(const Pattern&) {
  throw NotImplemented("SimTester::run_pattern [M4]");
}

FunctionalResult SimTester::run_program(const MemoryImage&, const RunOpts&) {
  throw NotImplemented("SimTester::run_program [M4]");
}

double SimTester::measure_time(const EdgeRef&, const EdgeRef&) {
  throw NotImplemented("SimTester::measure_time [M4]");
}

double SimTester::force_i_measure_v(std::string_view, double) {
  throw NotImplemented("SimTester::force_i_measure_v [M6]");
}

double SimTester::force_v_measure_i(std::string_view, double) {
  throw NotImplemented("SimTester::force_v_measure_i [M6]");
}

double SimTester::measure_icc() { throw NotImplemented("SimTester::measure_icc [M6]"); }

}  // namespace i8086::tester
