#include <gtest/gtest.h>

#include "i8086/golden/emulator.hpp"

namespace i8086::golden {
namespace {

TEST(Golden, ResetState) {
  const Registers r = reset_registers();
  EXPECT_EQ(r.cs, 0xFFFF);
  EXPECT_EQ(r.ip, 0x0000);
  EXPECT_EQ(r.flags, 0x0000);
}

}  // namespace
}  // namespace i8086::golden
