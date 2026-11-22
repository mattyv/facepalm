#include "src/linux/preview_jpeg.h"

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

// libjpeg's own headers assume C linkage and bring no C++ wrapper.
extern "C" {
#include <jpeglib.h>
}

#include "src/frame.h"

namespace facepalm {
namespace {

// libjpeg reports a fatal error by calling error_exit, which must not
// return (its default prints a message and calls exit(), unusable in a
// long-running daemon); longjmp back to EncodePreviewJpeg instead, the
// pattern libjpeg's own documentation (example.c) recommends.
struct JpegErrorHandler {
  jpeg_error_mgr base;
  std::jmp_buf recovery;
};

void OnJpegError(j_common_ptr cinfo) {
  longjmp(reinterpret_cast<JpegErrorHandler*>(cinfo->err)->recovery, 1);
}

}  // namespace

std::optional<std::string> EncodePreviewJpeg(const Frame& frame, float quality) {
  if (frame.width <= 0 || frame.height <= 0 || frame.bgra == nullptr) {
    return std::nullopt;
  }
  jpeg_compress_struct cinfo{};
  JpegErrorHandler error{};
  cinfo.err = jpeg_std_error(&error.base);
  error.base.error_exit = OnJpegError;
  unsigned char* buffer = nullptr;
  unsigned long buffer_size = 0;
  std::optional<std::string> encoded;
  // setjmp/longjmp crosses jpeg_create_compress, so nothing after this
  // point may hold a C++ object libjpeg's longjmp would skip the
  // destructor of; `row` and `encoded` are both declared before it.
  std::vector<unsigned char> row(static_cast<std::size_t>(frame.width) * 3);
  if (setjmp(error.recovery) != 0) {
    jpeg_destroy_compress(&cinfo);
    if (buffer != nullptr) {
      std::free(buffer);
    }
    return std::nullopt;
  }
  jpeg_create_compress(&cinfo);
  jpeg_mem_dest(&cinfo, &buffer, &buffer_size);
  cinfo.image_width = static_cast<JDIMENSION>(frame.width);
  cinfo.image_height = static_cast<JDIMENSION>(frame.height);
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  // Clamped the same way src/macos/preview_jpeg.h's ImageIO-backed
  // sibling clamps an out-of-range or non-finite quality, rather than
  // risking undefined behavior converting a non-finite float to int.
  constexpr float kMinQuality = 0.01F;
  constexpr float kMaxQuality = 1.0F;
  const float clamped = std::isfinite(quality)
                            ? std::clamp(quality, kMinQuality, kMaxQuality)
                            : kMaxQuality;
  jpeg_set_quality(&cinfo, static_cast<int>(clamped * 100.0F), TRUE);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    const std::uint8_t* source =
        frame.bgra + static_cast<std::size_t>(cinfo.next_scanline) * frame.bytes_per_row;
    for (int x = 0; x < frame.width; ++x) {
      // BGRA to RGB: libjpeg's JCS_RGB has no alpha channel of its own.
      row[static_cast<std::size_t>(x) * 3 + 0] = source[static_cast<std::size_t>(x) * 4 + 2];
      row[static_cast<std::size_t>(x) * 3 + 1] = source[static_cast<std::size_t>(x) * 4 + 1];
      row[static_cast<std::size_t>(x) * 3 + 2] = source[static_cast<std::size_t>(x) * 4 + 0];
    }
    JSAMPROW rows[1] = {row.data()};
    jpeg_write_scanlines(&cinfo, rows, 1);
  }
  jpeg_finish_compress(&cinfo);
  encoded = std::string(reinterpret_cast<const char*>(buffer), buffer_size);
  jpeg_destroy_compress(&cinfo);
  std::free(buffer);
  return encoded;
}

}  // namespace facepalm
