#include "src/face_analyzer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"

namespace facepalm {

FaceAnalyzer::FaceAnalyzer(std::unique_ptr<FaceModel> model,
                           std::vector<std::unique_ptr<FaceReader>> readers)
    : model_(std::move(model)), readers_(std::move(readers)) {}

bool FaceAnalyzer::Analyze(std::span<const std::uint8_t> rgba, int width,
                           int height, std::int64_t timestamp_ms,
                           std::vector<Reaction>& reactions,
                           std::string& error) {
  const FaceModel::Result result =
      model_->Detect(rgba, width, height, timestamp_ms, face_, error);
  if (result == FaceModel::Result::kFailed) {
    return false;
  }
  if (result == FaceModel::Result::kFace) {
    for (const auto& reader : readers_) {
      reader->OnFace(face_, width, height, timestamp_ms, reactions);
    }
    return true;
  }
  for (const auto& reader : readers_) {
    reader->OnNoFace();
  }
  return true;
}

}  // namespace facepalm
