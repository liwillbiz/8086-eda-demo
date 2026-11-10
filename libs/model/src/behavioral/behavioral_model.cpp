#include "behavioral/core.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/model/fault_injector.hpp"

namespace i8086::model {

std::unique_ptr<DeviceModel> make_behavioral(const spec::Spec& spec, const DieProfile& die,
                                             FaultSet faults) {
  faults.faults.insert(faults.faults.end(), die.faults.faults.begin(), die.faults.faults.end());
  FaultSet logic;
  for (const LogicBug* b : faults.all<LogicBug>()) logic.faults.emplace_back(*b);
  auto core = std::make_unique<detail::Core>(spec, die, std::move(logic));
  return std::make_unique<FaultInjector>(spec, std::move(core), std::move(faults));
}

}  // namespace i8086::model
