#include <gtest/gtest.h>

#include "i8086/tester/memory_image.hpp"
#include "i8086/tester/search.hpp"

namespace i8086::tester {
namespace {

TEST(SearchPassFail, FindsUpperBoundary) {
  const double edge = search_pass_fail([](double x) { return x <= 37.25; }, 0.0, 100.0, 0.01);
  EXPECT_LE(edge, 37.25);
  EXPECT_NEAR(edge, 37.25, 0.01);
}

TEST(SearchPassFail, SearchesDownward) {
  const double vmin = search_pass_fail([](double v) { return v >= 4.2; }, 5.5, 3.0, 0.001);
  EXPECT_GE(vmin, 4.2);
  EXPECT_NEAR(vmin, 4.2, 0.001);
}

TEST(SearchPassFail, RejectsFailingStart) {
  EXPECT_THROW(search_pass_fail([](double) { return false; }, 0.0, 1.0, 0.1), std::domain_error);
}

TEST(MemoryImage, WrapsAt1MiB) {
  MemoryImage img;
  const uint8_t bytes[] = {0xEA, 0x00};
  img.load(0xFFFFF, bytes);
  EXPECT_EQ(img.read(0xFFFFF), 0xEA);
  EXPECT_EQ(img.read(0x00000), 0x00);
  EXPECT_EQ(img.read(0x12345), std::nullopt);
}

}  // namespace
}  // namespace i8086::tester
