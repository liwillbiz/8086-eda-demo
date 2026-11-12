#pragma once

// Test ROMs (testprog/roms/*.asm, assembled by NASM at build time) and patterns
// (testprog/patterns/*.pat), embedded so that i86test needs no paths at run time.

#include <cstdint>
#include <span>
#include <string_view>

namespace i8086::testprog::embedded {

struct Blob {
  std::string_view name;  // file stem, e.g. "d1_add_flags"
  std::span<const uint8_t> bytes;
};

std::span<const Blob> roms();
std::span<const Blob> patterns();
bool nasm_found();  // false: the build had no NASM, so roms() is empty

}  // namespace i8086::testprog::embedded
