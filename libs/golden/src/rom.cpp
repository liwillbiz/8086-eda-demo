#include "i8086/golden/rom.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace i8086::golden {

std::vector<uint8_t> read_binary(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open ROM " + path.string());
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
  if (f.bad()) throw std::runtime_error("cannot read ROM " + path.string());
  return bytes;
}

Program rom_program(std::vector<uint8_t> rom, std::map<uint16_t, uint16_t> io_inputs) {
  if (rom.empty() || rom.size() > kMemoryTop) {
    throw std::invalid_argument("ROM size " + std::to_string(rom.size()) +
                                " is outside 1..1048576 bytes");
  }
  Program p;
  const auto base = static_cast<uint32_t>(kMemoryTop - rom.size());
  p.segments.emplace_back(base, std::move(rom));
  p.io_inputs = std::move(io_inputs);
  return p;
}

Program load_rom(const std::filesystem::path& path, std::map<uint16_t, uint16_t> io_inputs) {
  return rom_program(read_binary(path), std::move(io_inputs));
}

}  // namespace i8086::golden
