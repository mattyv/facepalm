#include "src/head_pose_analyzer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/reaction.h"

namespace facepalm {

HeadPoseAnalyzer::HeadPoseAnalyzer(std::unique_ptr<FaceModel> model)
    : model_(std::move(model)) {}

bool HeadPoseAnalyzer::Analyze(std::span<const std::uint8_t> rgba, int width,
                               int height, std::int64_t timestamp_ms,
                               std::vector<Reaction>& reactions,
                               std::string& error) {
  const FaceModel::Result result =
      model_->Detect(rgba, width, height, timestamp_ms, face_, error);
  if (result == FaceModel::Result::kFace) {
    reactions.push_back(ReactionFrom(face_));
  }
  return result != FaceModel::Result::kFailed;
}

}  // namespace facepalm
