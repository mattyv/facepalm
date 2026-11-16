// Implements facepalm_mediapipe.h over MediaPipe's C++ FaceLandmarker. Built
// only inside MediaPipe's Bazel workspace, where tools/fixtures/build.sh links
// this directory as the package //facepalm_shim.

#include "facepalm_shim/facepalm_mediapipe.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mediapipe/framework/formats/image_format.pb.h"
#include "mediapipe/tasks/cc/vision/core/running_mode.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/cc/vision/utils/image_utils.h"

using ::mediapipe::tasks::vision::CreateImageFromBuffer;
using ::mediapipe::tasks::vision::core::RunningMode;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarker;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerOptions;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerResult;

struct FpFaceModel {
  std::unique_ptr<FaceLandmarker> landmarker;
};

namespace {

// Writes `status` into the caller's buffer, truncated to fit.
int Fail(const absl::Status& status, char* error, size_t error_size) {
  if (error != nullptr && error_size > 0) {
    std::snprintf(error, error_size, "%s", status.ToString().c_str());
  }
  return -1;
}

// Copies the first face of `result` into `out`, or fails when its shape is
// not the one facepalm_mediapipe.h promises.
absl::Status CopyFace(const FaceLandmarkerResult& result, FpFace& out) {
  const auto& landmarks = result.face_landmarks[0].landmarks;
  if (landmarks.size() != FP_LANDMARKS || !result.face_blendshapes ||
      !result.facial_transformation_matrixes) {
    return absl::InternalError("unexpected face landmarker result shape");
  }
  for (int i = 0; i < FP_LANDMARKS; ++i) {
    out.landmarks[i][0] = landmarks[i].x;
    out.landmarks[i][1] = landmarks[i].y;
    out.landmarks[i][2] = landmarks[i].z;
  }
  const auto& categories = (*result.face_blendshapes)[0].categories;
  if (categories.size() != FP_BLENDSHAPES) {
    return absl::InternalError("unexpected blendshape count");
  }
  for (int i = 0; i < FP_BLENDSHAPES; ++i) {
    out.blendshapes[i] = categories[i].score;
  }
  // Eigen stores the matrix column by column, as MatrixData does.
  const auto& pose = (*result.facial_transformation_matrixes)[0];
  if (pose.size() != FP_POSE_VALUES) {
    return absl::InternalError("unexpected pose matrix size");
  }
  std::copy_n(pose.data(), FP_POSE_VALUES, out.pose_column_major);
  return absl::OkStatus();
}

}  // namespace

extern "C" int fp_face_model_create(const char* model_path, FpFaceModel** out,
                                    char* error, size_t error_size) {
  auto options = std::make_unique<FaceLandmarkerOptions>();
  options->base_options.model_asset_path = model_path;
  options->running_mode = RunningMode::VIDEO;
  options->num_faces = 1;
  options->output_face_blendshapes = true;
  options->output_facial_transformation_matrixes = true;
  absl::StatusOr<std::unique_ptr<FaceLandmarker>> landmarker =
      FaceLandmarker::Create(std::move(options));
  if (!landmarker.ok()) return Fail(landmarker.status(), error, error_size);
  *out = new FpFaceModel{std::move(*landmarker)};
  return 0;
}

extern "C" int fp_face_model_detect(FpFaceModel* model, const uint8_t* rgba,
                                    int width, int height, int64_t timestamp_ms,
                                    FpFace* out, char* error,
                                    size_t error_size) {
  // Wraps the caller's pixels in place, without copying them.
  absl::StatusOr<mediapipe::Image> image =
      CreateImageFromBuffer(mediapipe::ImageFormat::SRGBA, rgba, width, height);
  if (!image.ok()) return Fail(image.status(), error, error_size);
  absl::StatusOr<FaceLandmarkerResult> result =
      model->landmarker->DetectForVideo(*std::move(image), timestamp_ms);
  if (!result.ok()) return Fail(result.status(), error, error_size);
  if (result->face_landmarks.empty()) return 0;
  const absl::Status copied = CopyFace(*result, *out);
  if (!copied.ok()) return Fail(copied, error, error_size);
  return 1;
}

extern "C" void fp_face_model_destroy(FpFaceModel* model) {
  if (model == nullptr) return;
  // Close stops MediaPipe's graph; its status matters to no one here.
  static_cast<void>(model->landmarker->Close());
  delete model;
}
