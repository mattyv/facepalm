#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>

#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"
#include "src/mediapipe_shim/facepalm_mediapipe.h"

struct FpFaceModel {
  MpFaceLandmarkerPtr task;
  bool processed_frame = false;
};

namespace {

int Fail(const char* message, char* error, std::size_t size) {
  if (error != nullptr && size > 0) {
    std::snprintf(error, size, "%s", message);
  }
  return -1;
}

int MediaPipeFailure(char* reason, char* error, std::size_t size) {
  const int result =
      Fail(reason != nullptr ? reason : "MediaPipe failed", error, size);
  if (reason != nullptr) {
    MpErrorFree(reason);
  }
  return result;
}

int CopyFace(const FaceLandmarkerResult& result, FpFace& out, char* error,
             std::size_t size) {
  if (result.face_landmarks_count == 0) {
    return 0;
  }
  if (result.face_landmarks_count != 1 || result.face_blendshapes_count != 1 ||
      result.facial_transformation_matrixes_count != 1) {
    return Fail("unexpected MediaPipe face count", error, size);
  }
  const auto& landmarks = result.face_landmarks[0];
  const auto& shapes = result.face_blendshapes[0];
  const auto& matrix = result.facial_transformation_matrixes[0];
  if (landmarks.landmarks_count != FP_LANDMARKS ||
      shapes.categories_count != FP_BLENDSHAPES || matrix.rows != 4 ||
      matrix.cols != 4) {
    return Fail("unexpected MediaPipe result shape", error, size);
  }
  for (int i = 0; i < FP_LANDMARKS; ++i) {
    out.landmarks[i][0] = landmarks.landmarks[i].x;
    out.landmarks[i][1] = landmarks.landmarks[i].y;
    out.landmarks[i][2] = landmarks.landmarks[i].z;
  }
  for (int i = 0; i < FP_BLENDSHAPES; ++i) {
    out.blendshapes[i] = shapes.categories[i].score;
  }
  std::copy_n(matrix.data, FP_POSE_VALUES, out.pose_column_major);
  return 1;
}

}  // namespace

extern "C" int fp_face_model_create(const char* model_path, FpFaceModel** out,
                                    char* error, size_t error_size) {
  // RunningMode has no zero-valued enumerator, but running_mode is set below.
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
  FaceLandmarkerOptions options{};
  options.base_options.model_asset_path = model_path;
  options.base_options.host_system = HOST_SYSTEM_LINUX;
  options.running_mode = VIDEO;
  options.output_face_blendshapes = true;
  options.output_facial_transformation_matrixes = true;
  auto model = std::make_unique<FpFaceModel>();
  char* reason = nullptr;
  if (MpFaceLandmarkerCreate(&options, &model->task, &reason) != kMpOk) {
    return MediaPipeFailure(reason, error, error_size);
  }
  *out = model.release();
  return 0;
}

// facepalm_mediapipe.h's own signature, shared with the macOS shim.
extern "C" int fp_face_model_detect(
    FpFaceModel* model, const uint8_t* rgba,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    int width, int height, int64_t timestamp_ms, FpFace* out, char* error,
    size_t error_size) {
  if (width <= 0 || height <= 0 ||
      width > std::numeric_limits<int>::max() / 4 / height) {
    return Fail("invalid MediaPipe dimensions", error, error_size);
  }
  MpImagePtr image = nullptr;
  char* reason = nullptr;
  if (MpImageCreateFromUint8Data(kMpImageFormatSrgba, width, height, rgba,
                                 width * height * 4, &image,
                                 &reason) != kMpOk) {
    return MediaPipeFailure(reason, error, error_size);
  }
  FaceLandmarkerResult result{};
  const auto status = MpFaceLandmarkerDetectForVideo(
      model->task, image, nullptr, timestamp_ms, &result, &reason);
  MpImageFree(image);
  const int copied = status == kMpOk
                         ? CopyFace(result, *out, error, error_size)
                         : MediaPipeFailure(reason, error, error_size);
  MpFaceLandmarkerCloseResult(&result);
  if (copied >= 0 && !model->processed_frame) {
    model->processed_frame = true;
    std::fprintf(stderr, "facepalm-daemon: first camera frame analyzed\n");
  }
  return copied;
}

// facepalm_mediapipe.h's own signature, shared with the macOS shim; the
// pointer is deleted, not just read.
// NOLINTNEXTLINE(misc-const-correctness)
extern "C" void fp_face_model_destroy(FpFaceModel* model) {
  if (model == nullptr) {
    return;
  }
  char* reason = nullptr;
  static_cast<void>(MpFaceLandmarkerClose(model->task, &reason));
  if (reason != nullptr) {
    MpErrorFree(reason);
  }
  delete model;
}
