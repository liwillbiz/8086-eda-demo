#pragma once

// FROZEN INTERFACE (M0).

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>

namespace i8086::tester {

// Sparse 1 MiB memory served by the tester in memory-emulation mode.
class MemoryImage {
 public:
  static constexpr uint32_t kAddressMask = 0xFFFFF;

  void load(uint32_t addr, std::span<const uint8_t> bytes);
  void load_file(uint32_t addr, const std::filesystem::path&);
  std::optional<uint8_t> read(uint32_t addr) const;  // nullopt = unmapped
  const std::map<uint32_t, uint8_t>& bytes() const { return bytes_; }

 private:
  std::map<uint32_t, uint8_t> bytes_;
};

}  // namespace i8086::tester
