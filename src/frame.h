#ifndef FACEPALM_SRC_FRAME_H_
#define FACEPALM_SRC_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace facepalm {

// One captured frame, valid only during the call that hands it over.
struct Frame {
  int width = 0;
  int height = 0;
  std::size_t bytes_per_row = 0;
  const std::uint8_t* bgra = nullptr;  // 32BGRA, as MediaPipe's camera uses.
  // When the camera captured it, in milliseconds on the capture clock.
  // MediaPipe's VIDEO mode needs these to increase, and smooths with them.
  std::int64_t timestamp_ms = 0;
  // The camera's open attempt that captured it, stamped at the source, so a
  // frame delayed past a close and reopen is refused (#40).
  std::uint64_t attempt = 0;
};

// Copies `frame` into `rgba` as packed RGBA, width * 4 bytes a row, swapping
// the channels in the same pass: MediaPipe takes SRGBA but not BGRA
// (image_to_tensor_converter_opencv.cc). Reuses `rgba`'s storage.
void ToRgba(const Frame& frame, std::vector<std::uint8_t>& rgba);

}  // namespace facepalm

#endif  // FACEPALM_SRC_FRAME_H_
