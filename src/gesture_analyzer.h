#ifndef FACEPALM_SRC_GESTURE_ANALYZER_H_
#define FACEPALM_SRC_GESTURE_ANALYZER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_detector.h"
#include "src/frame_analyzer.h"
#include "src/nodding_pigeon.h"

namespace facepalm {

// Detects nods and head shakes with Nodding Pigeon (#34): each frame with
// exactly one face goes into its 60-frame window, and the model runs on the
// window each time it changes. Reacts with {"gesture":"nod"} or
// {"gesture":"shake"} once when a gesture starts, not again while it lasts.
class GestureAnalyzer : public FrameAnalyzer {
 public:
  // Nodding Pigeon's thresholds; their defaults live in Config.
  struct Thresholds {
    float motion;
    float gesture;
  };

  GestureAnalyzer(std::unique_ptr<FaceDetector> detector,
                  Thresholds thresholds);

  [[nodiscard]] bool Analyze(std::span<const std::uint8_t> rgba, int width,
                             int height, std::int64_t timestamp_ms,
                             std::vector<Reaction>& reactions,
                             std::string& error) override;

 private:
  std::unique_ptr<FaceDetector> detector_;
  Thresholds thresholds_;
  nodding_pigeon::Window window_;
  // The last gesture reported, or kStationary since the head was last still.
  nodding_pigeon::Gesture reported_ = nodding_pigeon::Gesture::kStationary;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_GESTURE_ANALYZER_H_
