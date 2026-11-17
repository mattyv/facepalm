#include "src/face_detector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm {
namespace {

constexpr std::size_t kErrorSize = 512;
constexpr std::size_t kChannels = 4;

}  // namespace

std::unique_ptr<MediaPipeFaceDetector> MediaPipeFaceDetector::Create(
    const std::string& model_path, std::string& error) {
  std::array<char, kErrorSize> reason{};
  FpFaceDetector* detector = nullptr;
  if (fp_face_detector_create(model_path.c_str(), &detector, reason.data(),
                              reason.size()) != 0) {
    error = reason.data();
    return nullptr;
  }
  return std::unique_ptr<MediaPipeFaceDetector>(
      new MediaPipeFaceDetector(detector));
}

MediaPipeFaceDetector::~MediaPipeFaceDetector() {
  fp_face_detector_destroy(detector_);
}

std::optional<int> MediaPipeFaceDetector::Detect(
    std::span<const std::uint8_t> rgba, int width, int height, Detection& first,
    std::string& error) {
  // The shim reads width * height * 4 bytes, so a short buffer is refused here.
  const std::size_t expected = static_cast<std::size_t>(width) *
                               static_cast<std::size_t>(height) * kChannels;
  if (width <= 0 || height <= 0 || rgba.size() != expected) {
    error = "expected " + std::to_string(expected) + " bytes of RGBA for " +
            std::to_string(width) + "x" + std::to_string(height) + ", got " +
            std::to_string(rgba.size());
    return std::nullopt;
  }
  std::array<char, kErrorSize> reason{};
  const int faces =
      fp_face_detector_detect(detector_, rgba.data(), width, height, &first,
                              reason.data(), reason.size());
  if (faces < 0) {
    error = reason.data();
    return std::nullopt;
  }
  return faces;
}

}  // namespace facepalm
