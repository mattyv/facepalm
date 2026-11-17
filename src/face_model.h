#ifndef FACEPALM_SRC_FACE_MODEL_H_
#define FACEPALM_SRC_FACE_MODEL_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm {

// One face: landmarks, blendshapes and the facial transformation matrix.
// ponytail: the shim's plain C struct, used as is, so a face crosses from the
// model to the pipeline without a copy into a type of our own.
using Face = FpFace;

// Finds the face in a frame. The interface the frame pipeline uses, so that
// its tests run without the model.
class FaceModel {
 public:
  enum class Result : std::uint8_t { kFace, kNoFace, kFailed };

  virtual ~FaceModel() = default;

  // Looks for a face in `rgba`, packed, width * height * 4 bytes. On kFace
  // fills `face`; on kFailed sets `error`. `timestamp_ms` must increase from
  // call to call. Calls must not overlap.
  [[nodiscard]] virtual Result Detect(std::span<const std::uint8_t> rgba,
                                      int width, int height,
                                      std::int64_t timestamp_ms, Face& face,
                                      std::string& error) = 0;
};

// FaceModel over libfacepalm_mediapipe.dylib, MediaPipe's face landmarker in
// VIDEO mode. Its tests stand in for the dylib at link time.
class MediaPipeFaceModel : public FaceModel {
 public:
  // Loads the .task bundle at `model_path`. Returns null and sets `error`
  // when it cannot.
  static std::unique_ptr<MediaPipeFaceModel> Create(
      const std::string& model_path, std::string& error);

  ~MediaPipeFaceModel() override;
  MediaPipeFaceModel(const MediaPipeFaceModel&) = delete;
  MediaPipeFaceModel& operator=(const MediaPipeFaceModel&) = delete;

  [[nodiscard]] Result Detect(std::span<const std::uint8_t> rgba, int width,
                              int height, std::int64_t timestamp_ms, Face& face,
                              std::string& error) override;

 private:
  explicit MediaPipeFaceModel(FpFaceModel* model) : model_(model) {}

  FpFaceModel* model_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FACE_MODEL_H_
