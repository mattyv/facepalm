#include "src/face_model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm {
namespace {

constexpr std::size_t kErrorSize = 512;
constexpr std::size_t kChannels = 4;

}  // namespace

std::unique_ptr<MediaPipeFaceModel> MediaPipeFaceModel::Create(
    const std::string& model_path, std::string& error) {
  std::array<char, kErrorSize> reason{};
  FpFaceModel* model = nullptr;
  if (fp_face_model_create(model_path.c_str(), &model, reason.data(),
                           reason.size()) != 0) {
    error = reason.data();
    return nullptr;
  }
  return std::unique_ptr<MediaPipeFaceModel>(new MediaPipeFaceModel(model));
}

MediaPipeFaceModel::~MediaPipeFaceModel() { fp_face_model_destroy(model_); }

FaceModel::Result MediaPipeFaceModel::Detect(std::span<const std::uint8_t> rgba,
                                             int width, int height,
                                             std::int64_t timestamp_ms,
                                             Face& face, std::string& error) {
  // The shim reads width * height * 4 bytes, so a short buffer is refused here.
  const std::size_t expected = static_cast<std::size_t>(width) *
                               static_cast<std::size_t>(height) * kChannels;
  if (width <= 0 || height <= 0 || rgba.size() != expected) {
    error = "expected " + std::to_string(expected) + " bytes of RGBA for " +
            std::to_string(width) + "x" + std::to_string(height) + ", got " +
            std::to_string(rgba.size());
    return Result::kFailed;
  }
  std::array<char, kErrorSize> reason{};
  switch (fp_face_model_detect(model_, rgba.data(), width, height, timestamp_ms,
                               &face, reason.data(), reason.size())) {
    case 1:
      return Result::kFace;
    case 0:
      return Result::kNoFace;
    default:
      error = reason.data();
      return Result::kFailed;
  }
}

}  // namespace facepalm
