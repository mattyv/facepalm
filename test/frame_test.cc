#include "src/frame.h"

#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(ToRgbaTest, SwapsBlueAndRedAndKeepsGreenAndAlpha) {
  const std::vector<std::uint8_t> bgra = {1, 2, 3, 4};
  const Frame frame{
      .width = 1, .height = 1, .bytes_per_row = 4, .bgra = bgra.data()};
  std::vector<std::uint8_t> rgba;

  ToRgba(frame, rgba);

  EXPECT_THAT(rgba, ElementsAre(3, 2, 1, 4));
}

TEST(ToRgbaTest, DropsTheRowPadding) {
  // Two rows of two pixels, each row padded by 3 bytes.
  const std::vector<std::uint8_t> bgra = {
      10, 11, 12, 13, 20, 21, 22, 23, 99, 99, 99,  //
      30, 31, 32, 33, 40, 41, 42, 43, 99, 99, 99,
  };
  const Frame frame{
      .width = 2, .height = 2, .bytes_per_row = 11, .bgra = bgra.data()};
  std::vector<std::uint8_t> rgba;

  ToRgba(frame, rgba);

  EXPECT_THAT(rgba, ElementsAre(12, 11, 10, 13, 22, 21, 20, 23,  //
                                32, 31, 30, 33, 42, 41, 40, 43));
}

TEST(ToRgbaTest, ReplacesWhatTheBufferHeld) {
  const std::vector<std::uint8_t> bgra = {1, 2, 3, 4};
  const Frame frame{
      .width = 1, .height = 1, .bytes_per_row = 4, .bgra = bgra.data()};
  std::vector<std::uint8_t> rgba(64, 7);

  ToRgba(frame, rgba);

  EXPECT_THAT(rgba, ElementsAre(3, 2, 1, 4));
}

TEST(ToRgbaTest, EmptyFrameGivesAnEmptyBuffer) {
  const Frame frame;
  std::vector<std::uint8_t> rgba = {1, 2};

  ToRgba(frame, rgba);

  EXPECT_THAT(rgba, IsEmpty());
}

}  // namespace
}  // namespace facepalm
