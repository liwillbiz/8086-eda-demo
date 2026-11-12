#pragma once

// FROZEN INTERFACE (M0).

#include <map>
#include <memory>

#include "i8086/spec/spec.hpp"
#include "i8086/tester/electrical_model.hpp"
#include "i8086/tester/tester.hpp"

namespace i8086::tester {

class SimTester final : public Tester {
 public:
  SimTester(const spec::Spec&, std::unique_ptr<model::DeviceModel>, ElectricalModel);

  void set_levels(const LevelSet&) override;
  void set_timing(int id, const TimingSet&) override;
  void set_temperature(double temp_c) override;
  void power(bool on) override;
  FunctionalResult run_pattern(const Pattern&) override;
  FunctionalResult run_program(const MemoryImage&, const RunOpts&) override;
  const BusTrace& last_trace() const override { return trace_; }
  double measure_time(const EdgeRef& from, const EdgeRef& to) override;
  double force_i_measure_v(std::string_view pin, double ma) override;
  double force_v_measure_i(std::string_view pin, double v) override;
  double measure_icc() override;

 private:
  spec::Spec spec_;
  std::unique_ptr<model::DeviceModel> dut_;
  ElectricalModel electrical_;
  LevelSet levels_;
  std::map<int, TimingSet> timings_;
  double temp_c_ = 25.0;
  bool powered_ = false;
  BusTrace trace_;
};

}  // namespace i8086::tester
