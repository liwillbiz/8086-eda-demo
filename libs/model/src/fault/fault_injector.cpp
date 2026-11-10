#include "i8086/model/fault_injector.hpp"

#include <algorithm>

namespace i8086::model {

bool FaultSet::has_logic_bug(std::string_view name) const {
  return std::ranges::any_of(all<LogicBug>(), [&](const LogicBug* b) { return b->name == name; });
}

FaultInjector::FaultInjector(const spec::Spec& spec, std::unique_ptr<DeviceModel> inner,
                             FaultSet faults)
    : spec_(spec), inner_(std::move(inner)), faults_(std::move(faults)) {}

// M0 stub: pass-through. M7 applies each pin-level fault here.
void FaultInjector::set_conditions(const Conditions& c) { inner_->set_conditions(c); }
void FaultInjector::set_inputs(std::span<const InputSample> in) { inner_->set_inputs(in); }
std::vector<PinEvent> FaultInjector::clock_edge(Edge e) { return inner_->clock_edge(e); }
ArchState FaultInjector::arch_state() const { return inner_->arch_state(); }
BusState FaultInjector::bus_state() const { return inner_->bus_state(); }

}  // namespace i8086::model
