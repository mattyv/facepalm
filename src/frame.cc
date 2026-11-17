#include "src/frame.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace facepalm {

namespace {
constexpr std::size_t kChannels = 4;
}  // namespace

void ToRgba(const Frame& frame, std::vector<std::uint8_t>& rgba) {
  const auto width = static_cast<std::size_t>(frame.width);
  const auto height = static_cast<std::size_t>(frame.height);
  rgba.resize(width * height * kChannels);
  std::uint8_t* out = rgba.data();
  for (std::size_t row = 0; row < height; ++row) {
    const std::uint8_t* in = frame.bgra + (row * frame.bytes_per_row);
    for (std::size_t column = 0; column < width;
         ++column, in += kChannels, out += kChannels) {
      out[0] = in[2];
      out[1] = in[1];
      out[2] = in[0];
      out[3] = in[3];
    }
  }
}

}  // namespace facepalm
