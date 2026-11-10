#include <vector>

#include "i8086/model/device_model.hpp"
#include "i8086/model/fault_injector.hpp"

namespace i8086::model {
namespace {

// M0 stub: idles in Ti and drives nothing. M3 implements the BIU/EU.
class BehavioralModel final : public DeviceModel {
 public:
  BehavioralModel(const DieProfile& die, FaultSet logic_faults)
      : die_(die), logic_faults_(std::move(logic_faults)) {}

  void set_conditions(const Conditions& c) override { conditions_ = c; }
  void set_inputs(std::span<const InputSample> in) override { inputs_.assign(in.begin(), in.end()); }

  std::vector<PinEvent> clock_edge(Edge e) override {
    if (e == Edge::Rising) ++bus_.clk;
    return {};
  }

  ArchState arch_state() const override { return arch_; }
  BusState bus_state() const override { return bus_; }

 private:
  DieProfile die_;
  FaultSet logic_faults_;
  Conditions conditions_;
  std::vector<InputSample> inputs_;
  ArchState arch_;
  BusState bus_;
};

}  // namespace

std::unique_ptr<DeviceModel> make_behavioral(const spec::Spec& spec, const DieProfile& die,
                                             FaultSet faults) {
  faults.faults.insert(faults.faults.end(), die.faults.faults.begin(), die.faults.faults.end());
  FaultSet logic;
  for (const LogicBug* b : faults.all<LogicBug>()) logic.faults.emplace_back(*b);
  auto core = std::make_unique<BehavioralModel>(die, std::move(logic));
  return std::make_unique<FaultInjector>(spec, std::move(core), std::move(faults));
}

}  // namespace i8086::model
