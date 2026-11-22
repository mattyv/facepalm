#include "src/eyes_probe.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

#include "src/face_model.h"

namespace facepalm {
namespace {

// MediaPipe's blendshape indices read here, in the order #139 lists them:
// eyeLookDown, In, Out and Up, each left eye then right, then eyeBlink,
// eyeSquint and eyeWide, each left then right.
constexpr std::array<int, 14> kBlendshapeIndices = {11, 12, 13, 14, 15, 16, 17,
                                                    18, 9,  10, 19, 20, 21, 22};
constexpr int kHighestBlendshapeIndex = 22;  // eyeWideRight.
static_assert(FP_BLENDSHAPES > kHighestBlendshapeIndex);

// The landmarks read here: the iris centres, then the eye corners, each
// left eye then right, in #139's order.
constexpr std::array<int, 6> kLandmarkIndices = {468, 473, 33, 133, 362, 263};
constexpr int kHighestLandmarkIndex = 473;  // The right iris centre.
static_assert(FP_LANDMARKS > kHighestLandmarkIndex);

constexpr std::size_t kBufferSize = 32;

// Appends ' ' then `value`, formatted with `format` (a printf conversion
// for one double), to `line`.
void AppendFormatted(std::string& line, const char* format, double value) {
  std::array<char, kBufferSize> formatted{};
  std::snprintf(formatted.data(), formatted.size(), format, value);
  line += ' ';
  line += formatted.data();
}

}  // namespace

std::string FormatEyesLine(const Face& face, std::int64_t timestamp_ms) {
  std::array<char, kBufferSize> head{};
  std::snprintf(head.data(), head.size(), "eyes %lld",
                static_cast<long long>(timestamp_ms));
  std::string line(head.data());
  for (const int index : kBlendshapeIndices) {
    AppendFormatted(line, "%.3f", static_cast<double>(face.blendshapes[index]));
  }
  for (const int index : kLandmarkIndices) {
    for (const float coordinate : face.landmarks[index]) {
      AppendFormatted(line, "%.6f", static_cast<double>(coordinate));
    }
  }
  return line;
}

}  // namespace facepalm
