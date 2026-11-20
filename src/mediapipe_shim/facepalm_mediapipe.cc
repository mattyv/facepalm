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
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "facepalm_shim/facepalm_mediapipe_internal.h"
#include "mediapipe/framework/calculator_framework.h"
#include "mediapipe/framework/formats/detection.pb.h"
#include "mediapipe/framework/formats/image_format.pb.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/framework/formats/location_data.pb.h"
#include "mediapipe/modules/face_detection/face_detection.pb.h"
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

// MediaPipe's legacy short-range face detection graph, the one
// mediapipe.solutions.face_detection runs and Nodding Pigeon reads, as
// mediapipe/modules/face_detection/face_detection_test.cc runs it. Unlike the
// Tasks FaceDetector, it gives the face box as fractions of the image rather
// than whole pixels (#34).
struct FpFaceDetector {
  mediapipe::CalculatorGraph graph;
  std::vector<mediapipe::Detection> detections;  // The last frame's.
  int64_t timestamp = 0;
};

namespace facepalm_shim {

int Fail(const absl::Status& status, char* error, size_t error_size) {
  if (error != nullptr && error_size > 0) {
    std::snprintf(error, error_size, "%s", status.ToString().c_str());
  }
  return -1;
}

int CopyFace(const FaceLandmarkerResult& result, FpFace& out, char* error,
             size_t error_size) {
  const auto& landmarks = result.face_landmarks[0].landmarks;
  if (landmarks.size() != FP_LANDMARKS || !result.face_blendshapes ||
      !result.facial_transformation_matrixes) {
    return Fail(absl::InternalError("unexpected face landmarker result shape"),
                error, error_size);
  }
  for (int i = 0; i < FP_LANDMARKS; ++i) {
    out.landmarks[i][0] = landmarks[i].x;
    out.landmarks[i][1] = landmarks[i].y;
    out.landmarks[i][2] = landmarks[i].z;
  }
  const auto& categories = (*result.face_blendshapes)[0].categories;
  if (categories.size() != FP_BLENDSHAPES) {
    return Fail(absl::InternalError("unexpected blendshape count"), error,
                error_size);
  }
  for (int i = 0; i < FP_BLENDSHAPES; ++i) {
    out.blendshapes[i] = categories[i].score;
  }
  // Eigen stores the matrix column by column, as MatrixData does.
  const auto& pose = (*result.facial_transformation_matrixes)[0];
  if (pose.size() != FP_POSE_VALUES) {
    return Fail(absl::InternalError("unexpected pose matrix size"), error,
                error_size);
  }
  std::copy_n(pose.data(), FP_POSE_VALUES, out.pose_column_major);
  return 1;
}

int CopyDetection(const std::vector<mediapipe::Detection>& detections,
                  FpDetection& out, char* error, size_t error_size) {
  const int faces = static_cast<int>(detections.size());
  if (faces == 0) return 0;
  const mediapipe::LocationData& location = detections[0].location_data();
  if (!location.has_relative_bounding_box() ||
      location.relative_keypoints_size() != FP_KEYPOINTS) {
    return Fail(absl::InternalError("unexpected face detector key points"),
                error, error_size);
  }
  // As mediapipe.solutions.face_detection gives it, and as video.py reads
  // it: the box's left and top clamped at 0.
  const auto& box = location.relative_bounding_box();
  out.box[0] = std::max(0.0F, box.xmin());
  out.box[1] = std::max(0.0F, box.ymin());
  out.box[2] = box.width();
  out.box[3] = box.height();
  for (int i = 0; i < FP_KEYPOINTS; ++i) {
    out.keypoints[i][0] = location.relative_keypoints(i).x();
    out.keypoints[i][1] = location.relative_keypoints(i).y();
  }
  return faces;
}

}  // namespace facepalm_shim

using ::facepalm_shim::CopyDetection;
using ::facepalm_shim::CopyFace;
using ::facepalm_shim::Fail;

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
  // Wraps the caller's pixels in place, without copying them. It fails only
  // for a format other than SRGB, SRGBA or SBGRA (image_utils.cc).
  mediapipe::Image image =
      CreateImageFromBuffer(mediapipe::ImageFormat::SRGBA, rgba, width, height)
          .value();
  absl::StatusOr<FaceLandmarkerResult> result =
      model->landmarker->DetectForVideo(std::move(image), timestamp_ms);
  if (!result.ok()) return Fail(result.status(), error, error_size);
  if (result->face_landmarks.empty()) return 0;
  return CopyFace(*result, *out, error, error_size);
}

extern "C" void fp_face_model_destroy(FpFaceModel* model) {
  if (model == nullptr) return;
  // Close stops MediaPipe's graph; its status matters to no one here.
  static_cast<void>(model->landmarker->Close());
  delete model;
}

extern "C" int fp_face_detector_create(const char* model_path,
                                       FpFaceDetector** out, char* error,
                                       size_t error_size) {
  // The graph is linked into this library, so it is always registered.
  mediapipe::CalculatorGraphConfig config =
      mediapipe::GraphRegistry()
          .CreateByName("", "FaceDetectionShortRangeCpu")
          .value();
  // The graph options mediapipe.solutions.face_detection sets, with the
  // model given by path instead of found among MediaPipe's resources.
  mediapipe::FaceDetectionOptions options;
  options.set_model_path(model_path);
  options.set_min_score_thresh(0.5F);
  config.clear_graph_options();
  config.add_graph_options()->PackFrom(options);
  auto detector = std::make_unique<FpFaceDetector>();
  // Update keeps the first error; a missing or bad model shows up as the
  // graph starts. Each later step on a failed graph fails harmlessly.
  absl::Status status = detector->graph.Initialize(config);
  status.Update(detector->graph.ObserveOutputStream(
      "detections", [raw = detector.get()](const mediapipe::Packet& packet) {
        raw->detections = packet.Get<std::vector<mediapipe::Detection>>();
        return absl::OkStatus();
      }));
  status.Update(detector->graph.StartRun({}));
  status.Update(detector->graph.WaitUntilIdle());
  if (!status.ok()) return Fail(status, error, error_size);
  *out = detector.release();
  return 0;
}

extern "C" int fp_face_detector_detect(FpFaceDetector* detector,
                                       const uint8_t* rgba, int width,
                                       int height, FpDetection* out,
                                       char* error, size_t error_size) {
  // Wraps the caller's pixels in place, read during the call only.
  auto frame = std::make_unique<mediapipe::ImageFrame>(
      mediapipe::ImageFormat::SRGBA, width, height, width * 4,
      const_cast<uint8_t*>(rgba), [](uint8_t*) {});
  detector->detections.clear();
  absl::Status status = detector->graph.AddPacketToInputStream(
      "image", mediapipe::Adopt(frame.release())
                   .At(mediapipe::Timestamp(++detector->timestamp)));
  status.Update(detector->graph.WaitUntilIdle());
  if (!status.ok()) return Fail(status, error, error_size);
  return CopyDetection(detector->detections, *out, error, error_size);
}

extern "C" void fp_face_detector_destroy(FpFaceDetector* detector) {
  if (detector == nullptr) return;
  // Closing stops the graph; its status matters to no one here.
  static_cast<void>(detector->graph.CloseAllPacketSources());
  static_cast<void>(detector->graph.WaitUntilDone());
  delete detector;
}
