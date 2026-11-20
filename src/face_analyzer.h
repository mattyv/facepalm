#ifndef FACEPALM_SRC_FACE_ANALYZER_H_
#define FACEPALM_SRC_FACE_ANALYZER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/frame_analyzer.h"

namespace facepalm {

// Runs the face model once a frame and gives its result to every reader:
// OnFace for a face, OnNoFace for none. Replaces HeadPoseAnalyzer, now that
// head pose and head gestures both need the one face, rather than each
// loading a model of its own (#34).
class FaceAnalyzer : public FrameAnalyzer {
 public:
  FaceAnalyzer(std::unique_ptr<FaceModel> model,
               std::vector<std::unique_ptr<FaceReader>> readers);

  [[nodiscard]] bool Analyze(std::span<const std::uint8_t> rgba, int width,
                             int height, std::int64_t timestamp_ms,
                             std::vector<Reaction>& reactions,
                             std::string& error) override;

 private:
  std::unique_ptr<FaceModel> model_;
  std::vector<std::unique_ptr<FaceReader>> readers_;
  Face face_{};
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FACE_ANALYZER_H_
