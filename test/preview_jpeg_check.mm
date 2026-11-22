#include "src/macos/preview_jpeg.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/frame.h"

namespace facepalm {
namespace {

using ::testing::Gt;

// A tiny 2x2 BGRA buffer: a real image, not a camera, so this needs
// nothing beyond ImageIO.
Frame SmallFrame(std::array<std::uint8_t, 16>& pixels) {
  pixels = {0,   0,   255, 0,  // Blue pixel (B G R X).
            0,   255, 0,   0,  // Green.
            255, 0,   0,   0,  // Red.
            128, 128, 128, 0};
  return Frame{.width = 2, .height = 2, .bytes_per_row = 8, .bgra = pixels.data()};
}

TEST(PreviewJpegTest, EncodesARealDecodableJpeg) {
  std::array<std::uint8_t, 16> pixels{};
  const Frame frame = SmallFrame(pixels);

  const std::optional<std::string> encoded = EncodePreviewJpeg(frame, 0.6F);

  ASSERT_TRUE(encoded.has_value());
  EXPECT_THAT(encoded->size(), Gt(0U));
  NSData* data = [NSData dataWithBytes:encoded->data() length:encoded->size()];
  CGImageSourceRef source = CGImageSourceCreateWithData((CFDataRef)data, nullptr);
  ASSERT_NE(source, nullptr);
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  EXPECT_NE(image, nullptr);
  EXPECT_EQ(CGImageGetWidth(image), 2U);
  EXPECT_EQ(CGImageGetHeight(image), 2U);
  if (image != nullptr) CGImageRelease(image);
  CFRelease(source);
}

TEST(PreviewJpegTest, AHigherQualityMakesAtLeastAsManyBytes) {
  std::array<std::uint8_t, 16> pixels{};
  const Frame frame = SmallFrame(pixels);

  const std::optional<std::string> low = EncodePreviewJpeg(frame, 0.1F);
  const std::optional<std::string> high = EncodePreviewJpeg(frame, 1.0F);

  ASSERT_TRUE(low.has_value());
  ASSERT_TRUE(high.has_value());
  EXPECT_GE(high->size(), low->size());
}

TEST(PreviewJpegTest, ANonFiniteQualityStillEncodes) {
  // ImageIO clamps an out-of-range or non-finite compression quality
  // rather than failing on it; this is the only way found to probe
  // CGImageDestinationFinalize with an unusual option, so it is recorded
  // as a non-failure rather than left unconsidered.
  std::array<std::uint8_t, 16> pixels{};
  const Frame frame = SmallFrame(pixels);

  EXPECT_TRUE(EncodePreviewJpeg(frame, std::nanf("")).has_value());
}

TEST(PreviewJpegTest, FailsOnAZeroSizedFrame) {
  const Frame frame{.width = 0, .height = 0, .bytes_per_row = 0, .bgra = nullptr};

  EXPECT_FALSE(EncodePreviewJpeg(frame, 0.6F).has_value());
}

}  // namespace
}  // namespace facepalm
