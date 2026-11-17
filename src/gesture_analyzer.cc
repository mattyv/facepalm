#include "src/gesture_analyzer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_detector.h"
#include "src/nodding_pigeon.h"

namespace facepalm {

using nodding_pigeon::Gesture;

GestureAnalyzer::GestureAnalyzer(std::unique_ptr<FaceDetector> detector,
                                 Thresholds thresholds)
    : detector_(std::move(detector)), thresholds_(thresholds) {}

bool GestureAnalyzer::Analyze(std::span<const std::uint8_t> rgba, int width,
                              int height, std::int64_t /*timestamp_ms*/,
                              std::vector<Reaction>& reactions,
                              std::string& error) {
  Detection detection{};
  const std::optional<int> faces =
      detector_->Detect(rgba, width, height, detection, error);
  if (!faces) {
    return false;
  }
  // video.py uses a frame only when it holds exactly one face.
  if (*faces != 1) {
    return true;
  }
  const auto frames = window_.Add(nodding_pigeon::RawFrameFrom(detection));
  if (!frames) {
    return true;
  }
  const Gesture gesture =
      nodding_pigeon::Classify(nodding_pigeon::Predict(*frames),
                               thresholds_.motion, thresholds_.gesture);
  if (gesture != last_ && gesture == Gesture::kNodding) {
    reactions.emplace_back(R"({"gesture":"nod"})");
  } else if (gesture != last_ && gesture == Gesture::kTurning) {
    reactions.emplace_back(R"({"gesture":"shake"})");
  }
  last_ = gesture;
  return true;
}

}  // namespace facepalm
