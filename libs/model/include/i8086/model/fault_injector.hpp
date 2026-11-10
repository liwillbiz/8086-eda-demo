#pragma once

// FROZEN INTERFACE (M0).

#include <memory>

#include "i8086/model/device_model.hpp"

namespace i8086::model {

// Decorator that applies pin-level faults (StuckAt, SlowTiming, SetupDegrade,
// LowVccFail) around any DeviceModel, keeping the model logic fault-free.
class FaultInjector final : public DeviceModel {
 public:
  FaultInjector(const spec::Spec&, std::unique_ptr<DeviceModel> inner, FaultSet);  // throws spec::SpecError
  ~FaultInjector() override;

  void set_conditions(const Conditions&) override;
  void set_inputs(std::span<const InputSample>) override;
  std::vector<PinEvent> clock_edge(Edge) override;
  ArchState arch_state() const override;
  BusState bus_state() const override;

  const FaultSet& faults() const { return faults_; }

 private:
  spec::Spec spec_;
  std::unique_ptr<DeviceModel> inner_;
  FaultSet faults_;
  struct State;  // per-run fault state, defined in fault_injector.cpp
  std::unique_ptr<State> state_;
};

}  // namespace i8086::model
