// Writes MediaPipe's own face landmarker results for each image as JSON, to
// be facepalm's golden fixtures. Built inside MediaPipe's Bazel workspace by
// tools/fixtures/build.sh; it is not part of facepalm's CMake build.
//
// Usage: face_landmarker_fixtures [--video] <model.task> <image>...
//
// With --video, the images are consecutive frames, 33 ms apart, in
// MediaPipe's VIDEO running mode, as facepalm's shim runs it: the face is
// tracked from frame to frame and its landmarks smoothed. Without it, each
// image stands alone, in IMAGE mode, as MediaPipe's golden results do.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "mediapipe/tasks/cc/vision/core/running_mode.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/cc/vision/utils/image_utils.h"

namespace {

using ::mediapipe::tasks::vision::DecodeImageFromFile;
using ::mediapipe::tasks::vision::core::RunningMode;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarker;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerOptions;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerResult;

// Formats a float with enough digits to read back the same value.
// The time between frames in VIDEO mode, about 30 frames a second.
constexpr int64_t kFrameMilliseconds = 33;

std::string Number(float value) { return absl::StrFormat("%.9g", value); }

// Formats the first face in `result` as one JSON object.
std::string FaceJson(const std::string& image,
                     const FaceLandmarkerResult& result) {
  std::string json = absl::StrCat("{\"image\": \"", image, "\"");
  if (result.face_landmarks.empty()) {
    return absl::StrCat(json, ", \"face\": null}");
  }
  std::string landmarks;
  for (const auto& landmark : result.face_landmarks[0].landmarks) {
    absl::StrAppend(&landmarks, landmarks.empty() ? "" : ", ", "[",
                    Number(landmark.x), ", ", Number(landmark.y), ", ",
                    Number(landmark.z), "]");
  }
  std::string blendshapes;
  for (const auto& category : (*result.face_blendshapes)[0].categories) {
    absl::StrAppend(&blendshapes, blendshapes.empty() ? "" : ", ", "\"",
                    category.category_name.value_or(""),
                    "\": ", Number(category.score));
  }
  // Eigen stores the matrix column by column, as MediaPipe's MatrixData does.
  const auto& matrix = (*result.facial_transformation_matrixes)[0];
  std::string pose;
  for (int i = 0; i < matrix.size(); ++i) {
    absl::StrAppend(&pose, i == 0 ? "" : ", ", Number(matrix.data()[i]));
  }
  return absl::StrCat(json, ",\n \"landmarks\": [", landmarks,
                      "],\n \"blendshapes\": {", blendshapes,
                      "},\n \"pose_column_major\": [", pose, "]}");
}

absl::Status Run(int argc, char** argv) {
  const bool video = argc > 1 && std::string(argv[1]) == "--video";
  if (video) {
    --argc;
    ++argv;
  }
  if (argc < 3) {
    return absl::InvalidArgumentError(
        "usage: face_landmarker_fixtures [--video] <model.task> <image>...");
  }
  auto options = std::make_unique<FaceLandmarkerOptions>();
  options->base_options.model_asset_path = argv[1];
  options->output_face_blendshapes = true;
  options->running_mode = video ? RunningMode::VIDEO : RunningMode::IMAGE;
  options->output_facial_transformation_matrixes = true;
  absl::StatusOr<std::unique_ptr<FaceLandmarker>> landmarker =
      FaceLandmarker::Create(std::move(options));
  if (!landmarker.ok()) return landmarker.status();
  std::printf("[\n");
  for (int i = 2; i < argc; ++i) {
    absl::StatusOr<mediapipe::Image> image = DecodeImageFromFile(argv[i]);
    if (!image.ok()) return image.status();
    absl::StatusOr<FaceLandmarkerResult> result =
        video ? (*landmarker)
                    ->DetectForVideo(*image, (i - 2) * kFrameMilliseconds)
              : (*landmarker)->Detect(*image);
    if (!result.ok()) return result.status();
    std::printf("%s%s\n", FaceJson(argv[i], *result).c_str(),
                i + 1 < argc ? "," : "");
  }
  std::printf("]\n");
  return (*landmarker)->Close();
}

}  // namespace

int main(int argc, char** argv) {
  const absl::Status status = Run(argc, argv);
  if (!status.ok()) {
    std::fprintf(stderr, "%s\n", status.ToString().c_str());
    return 1;
  }
  return 0;
}
