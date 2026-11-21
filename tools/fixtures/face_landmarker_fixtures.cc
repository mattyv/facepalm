// Writes MediaPipe's own face landmarker results for each image as JSON, to
// be facepalm's golden fixtures. Built inside MediaPipe's Bazel workspace by
// tools/fixtures/build.sh; it is not part of facepalm's CMake build.
//
// Usage: face_landmarker_fixtures [--video | --recording <timestamps.txt>]
//            <model.task> <image>...
//
// With --video, the images are consecutive frames, 33 ms apart, in
// MediaPipe's VIDEO running mode, as facepalm's shim runs it: the face is
// tracked from frame to frame and its landmarks smoothed. With --recording,
// they are frames of a camera_probe recording, in VIDEO mode at the capture
// times its timestamps.txt lists (src/frame_times.h), so a 15 fps recording
// plays at its own speed and a dropped frame leaves its gap (#42). Without
// either, each image stands alone, in IMAGE mode, as MediaPipe's golden
// results do.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "mediapipe/tasks/cc/vision/core/running_mode.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker.h"
#include "mediapipe/tasks/cc/vision/utils/image_utils.h"
#include "src/frame_times.h"

namespace {

using ::mediapipe::tasks::vision::DecodeImageFromFile;
using ::mediapipe::tasks::vision::core::RunningMode;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarker;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerOptions;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerResult;

// The time between frames with --video, about 30 frames a second.
constexpr int64_t kFrameMilliseconds = 33;

// Formats a float with enough digits to read back the same value.
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

// The VIDEO mode time of each of `images`: 33 ms apart for --video, or the
// capture times in the recording's `manifest_path` for --recording.
absl::StatusOr<std::vector<int64_t>> VideoTimes(
    const std::optional<std::string>& manifest_path,
    const std::vector<std::string>& images) {
  if (!manifest_path) {
    std::vector<int64_t> times;
    for (size_t i = 0; i < images.size(); ++i) {
      times.push_back(static_cast<int64_t>(i) * kFrameMilliseconds);
    }
    return times;
  }
  std::ifstream file(*manifest_path);
  if (!file) {
    return absl::NotFoundError(absl::StrCat("cannot read ", *manifest_path));
  }
  std::stringstream manifest;
  manifest << file.rdbuf();
  std::string error;
  std::optional<std::vector<int64_t>> times =
      facepalm::FrameTimes(manifest.str(), images, error);
  if (!times) return absl::InvalidArgumentError(error);
  return *std::move(times);
}

absl::Status Run(int argc, char** argv) {
  bool video = false;
  std::optional<std::string> manifest_path;
  if (argc > 2 && std::string(argv[1]) == "--recording") {
    video = true;
    manifest_path = argv[2];
    argc -= 2;
    argv += 2;
  } else if (argc > 1 && std::string(argv[1]) == "--video") {
    video = true;
    --argc;
    ++argv;
  }
  if (argc < 3) {
    return absl::InvalidArgumentError(
        "usage: face_landmarker_fixtures [--video | --recording "
        "<timestamps.txt>] <model.task> <image>...");
  }
  const std::vector<std::string> images(argv + 2, argv + argc);
  absl::StatusOr<std::vector<int64_t>> times =
      video ? VideoTimes(manifest_path, images) : std::vector<int64_t>();
  if (!times.ok()) return times.status();
  auto options = std::make_unique<FaceLandmarkerOptions>();
  options->base_options.model_asset_path = argv[1];
  options->output_face_blendshapes = true;
  options->running_mode = video ? RunningMode::VIDEO : RunningMode::IMAGE;
  options->output_facial_transformation_matrixes = true;
  absl::StatusOr<std::unique_ptr<FaceLandmarker>> landmarker =
      FaceLandmarker::Create(std::move(options));
  if (!landmarker.ok()) return landmarker.status();
  std::printf("[\n");
  for (size_t i = 0; i < images.size(); ++i) {
    absl::StatusOr<mediapipe::Image> image = DecodeImageFromFile(images[i]);
    if (!image.ok()) return image.status();
    absl::StatusOr<FaceLandmarkerResult> result =
        video ? (*landmarker)->DetectForVideo(*image, (*times)[i])
              : (*landmarker)->Detect(*image);
    if (!result.ok()) return result.status();
    std::printf("%s%s\n", FaceJson(images[i], *result).c_str(),
                i + 1 < images.size() ? "," : "");
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
