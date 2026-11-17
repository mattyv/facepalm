#ifndef FACEPALM_SRC_FACE_DETECTOR_H_
#define FACEPALM_SRC_FACE_DETECTOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm {

// One face's box and six key points, from the short-range face detector.
// ponytail: the shim's plain C struct, used as is, like Face.
using Detection = FpDetection;

// Finds faces in a frame for Nodding Pigeon (#34). An interface, so that
// what uses it is tested without the model.
class FaceDetector {
 public:
  virtual ~FaceDetector() = default;

  // Looks for faces in `rgba`, packed, width * height * 4 bytes. Returns how
  // many it found, filling `first` with the first, or nothing with `error`
  // set when it fails. Calls must not overlap.
  [[nodiscard]] virtual std::optional<int> Detect(
      std::span<const std::uint8_t> rgba, int width, int height,
      Detection& first, std::string& error) = 0;
};

// FaceDetector over libfacepalm_mediapipe.dylib. Its tests stand in for the
// dylib at link time.
class MediaPipeFaceDetector : public FaceDetector {
 public:
  // Loads the short-range detector's .tflite at `model_path`. Returns null
  // and sets `error` when it cannot.
  static std::unique_ptr<MediaPipeFaceDetector> Create(
      const std::string& model_path, std::string& error);

  ~MediaPipeFaceDetector() override;
  MediaPipeFaceDetector(const MediaPipeFaceDetector&) = delete;
  MediaPipeFaceDetector& operator=(const MediaPipeFaceDetector&) = delete;

  [[nodiscard]] std::optional<int> Detect(std::span<const std::uint8_t> rgba,
                                          int width, int height,
                                          Detection& first,
                                          std::string& error) override;

 private:
  explicit MediaPipeFaceDetector(FpFaceDetector* detector)
      : detector_(detector) {}

  FpFaceDetector* detector_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FACE_DETECTOR_H_
