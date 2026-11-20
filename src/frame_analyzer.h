#ifndef FACEPALM_SRC_FRAME_ANALYZER_H_
#define FACEPALM_SRC_FRAME_ANALYZER_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "src/camera_controller.h"

namespace facepalm {

// Something the frame pipeline runs on every frame, such as the face model
// with head-pose reactions, or Nodding Pigeon. The pipeline makes its
// analysers when the camera opens and destroys them when it closes, on the
// inference queue, so calls never overlap.
class FrameAnalyzer {
 public:
  virtual ~FrameAnalyzer() = default;

  // Analyses one frame of packed RGBA, width * height * 4 bytes, read during
  // the call only. Appends any reactions to `reactions`. Returns false with
  // `error` set when it fails.
  [[nodiscard]] virtual bool Analyze(std::span<const std::uint8_t> rgba,
                                     int width, int height,
                                     std::int64_t timestamp_ms,
                                     std::vector<Reaction>& reactions,
                                     std::string& error) = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FRAME_ANALYZER_H_
