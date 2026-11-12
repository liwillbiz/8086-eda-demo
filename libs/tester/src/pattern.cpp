#include "i8086/tester/pattern.hpp"

#include "i8086/common/errors.hpp"

namespace i8086::tester {

Pattern Pattern::parse(std::string_view, std::string_view origin) {
  throw NotImplemented("Pattern::parse(" + std::string(origin) + ") [M4]");
}

Pattern Pattern::load(const std::filesystem::path& path) {
  throw NotImplemented("Pattern::load(" + path.string() + ") [M4]");
}

std::string Pattern::to_text() const { throw NotImplemented("Pattern::to_text [M4]"); }

}  // namespace i8086::tester
