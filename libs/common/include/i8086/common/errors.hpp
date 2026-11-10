#pragma once

#include <stdexcept>
#include <string>

namespace i8086 {

// Thrown by stub implementations of frozen interfaces until the owning
// milestone lands. The runner reports it as Verdict::Error, never as Fail.
class NotImplemented : public std::logic_error {
 public:
  explicit NotImplemented(const std::string& what)
      : std::logic_error("not implemented: " + what) {}
};

}  // namespace i8086
