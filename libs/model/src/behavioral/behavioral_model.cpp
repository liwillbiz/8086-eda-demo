#include <algorithm>

#include "behavioral/core.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/model/fault_injector.hpp"

namespace i8086::model {

std::unique_ptr<DeviceModel> make_behavioral(const spec::Spec& spec, const DieProfile& die,
                                             FaultSet faults) {
  faults.faults.insert(faults.faults.end(), die.faults.faults.begin(), die.faults.faults.end());
  FaultSet logic;
  for (const LogicBug* b : faults.all<LogicBug>()) logic.faults.emplace_back(*b);
  // SlowTiming on the internal critical path (B1's Fmax) acts inside the core; the
  // FaultInjector only sees pin events.
  DieProfile slowed = die;
  if (const auto it = slowed.timing_ns.find("TCLCL_CRIT"); it != slowed.timing_ns.end()) {
    for (const SlowTiming* f : faults.all<SlowTiming>()) {
      if (f->params.empty() || std::ranges::find(f->params, "TCLCL_CRIT") != f->params.end()) {
        it->second = it->second * f->scale + f->add_ns;
      }
    }
  }
  auto core = std::make_unique<detail::Core>(spec, slowed, std::move(logic));
  return std::make_unique<FaultInjector>(spec, std::move(core), std::move(faults));
}

}  // namespace i8086::model
