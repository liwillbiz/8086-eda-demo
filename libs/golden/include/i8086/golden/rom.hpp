#pragma once

// ROM loading for the golden emulator. Additive to the frozen emulator.hpp (M2).

#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

#include "i8086/golden/emulator.hpp"

namespace i8086::golden {

inline constexpr uint32_t kMemoryTop = 0x100000;  // 1 MB physical address space

// Reads a flat binary (for example NASM `-f bin` output). Throws std::runtime_error if the
// file cannot be read.
std::vector<uint8_t> read_binary(const std::filesystem::path&);

// Places a boot ROM so that its last byte is at FFFFFh, which puts the reset entry point
// (FFFF0h) 16 bytes from its end. Throws std::invalid_argument if it is empty or over 1 MB.
Program rom_program(std::vector<uint8_t> rom, std::map<uint16_t, uint16_t> io_inputs = {});

// Same, reading the ROM from a file.
Program load_rom(const std::filesystem::path&, std::map<uint16_t, uint16_t> io_inputs = {});

}  // namespace i8086::golden
