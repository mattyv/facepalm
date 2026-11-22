#include "src/linux/preview_jpeg.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

extern "C" {
#include <jpeglib.h>
}

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/frame.h"

namespace facepalm {
namespace {

using ::testing::Gt;

// A tiny 2x2 BGRA buffer: a real image, not a camera, so this needs
// nothing beyond libjpeg, as test/preview_jpeg_check.mm's macOS sibling
// needs nothing beyond ImageIO.
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
  jpeg_decompress_struct dinfo{};
  jpeg_error_mgr error{};
  dinfo.err = jpeg_std_error(&error);
  jpeg_create_decompress(&dinfo);
  jpeg_mem_src(&dinfo, reinterpret_cast<const unsigned char*>(encoded->data()),
              static_cast<unsigned long>(encoded->size()));
  ASSERT_EQ(jpeg_read_header(&dinfo, TRUE), JPEG_HEADER_OK);
  EXPECT_EQ(dinfo.image_width, 2U);
  EXPECT_EQ(dinfo.image_height, 2U);
  jpeg_start_decompress(&dinfo);
  std::array<unsigned char, 6> row{};
  JSAMPROW rows[1] = {row.data()};
  while (dinfo.output_scanline < dinfo.output_height) {
    jpeg_read_scanlines(&dinfo, rows, 1);
  }
  jpeg_finish_decompress(&dinfo);
  jpeg_destroy_decompress(&dinfo);
}

TEST(PreviewJpegTest, AHigherQualityMakesAtLeastAsManyBytes) {
  std::array<std::uint8_t, 16> pixels{};
  const Frame frame = SmallFrame(pixels);

  const std::optional<std::string> low = EncodePreviewJpeg(frame, 0.01F);
  const std::optional<std::string> high = EncodePreviewJpeg(frame, 1.0F);

  ASSERT_TRUE(low.has_value());
  ASSERT_TRUE(high.has_value());
  EXPECT_GE(high->size(), low->size());
}

TEST(PreviewJpegTest, ANonFiniteQualityStillEncodes) {
  // Converting a non-finite float to int is undefined behavior, so
  // EncodePreviewJpeg clamps it first, the same non-failure
  // test/preview_jpeg_check.mm records for ImageIO's own clamping.
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
