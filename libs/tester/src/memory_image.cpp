#include "i8086/tester/memory_image.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace i8086::tester {

void MemoryImage::load(uint32_t addr, std::span<const uint8_t> bytes) {
  for (uint8_t b : bytes) bytes_[addr++ & kAddressMask] = b;
}

void MemoryImage::load_file(uint32_t addr, const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open memory image " + path.string());
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  load(addr, data);
}

std::optional<uint8_t> MemoryImage::read(uint32_t addr) const {
  const auto it = bytes_.find(addr & kAddressMask);
  if (it == bytes_.end()) return std::nullopt;
  return it->second;
}

}  // namespace i8086::tester
