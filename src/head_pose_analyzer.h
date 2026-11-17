#ifndef FACEPALM_SRC_HEAD_POSE_ANALYZER_H_
#define FACEPALM_SRC_HEAD_POSE_ANALYZER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/frame_analyzer.h"

namespace facepalm {

// Runs the face model on each frame and reacts to each face with its head's
// yaw, pitch and roll (#21).
class HeadPoseAnalyzer : public FrameAnalyzer {
 public:
  explicit HeadPoseAnalyzer(std::unique_ptr<FaceModel> model);

  [[nodiscard]] bool Analyze(std::span<const std::uint8_t> rgba, int width,
                             int height, std::int64_t timestamp_ms,
                             std::vector<Reaction>& reactions,
                             std::string& error) override;

 private:
  std::unique_ptr<FaceModel> model_;
  Face face_{};
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_HEAD_POSE_ANALYZER_H_
