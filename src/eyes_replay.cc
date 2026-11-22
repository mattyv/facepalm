#include "src/eyes_replay.h"

#include <array>
#include <cstdint>
#include <istream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace facepalm {
namespace {

// Reads the 14 blendshape scores into `frame`'s fields, in
// FormatEyesLine's order. Returns whether every one was read.
bool ReadScores(std::istringstream& row, EyesFrame& frame) {
  const std::array<std::array<double, 2>*, 7> scores = {
      &frame.eye_look_down, &frame.eye_look_in, &frame.eye_look_out,
      &frame.eye_look_up,   &frame.eye_blink,   &frame.eye_squint,
      &frame.eye_wide};
  for (std::array<double, 2>* pair : scores) {
    if (!(row >> pair->at(0) >> pair->at(1))) {
      return false;
    }
  }
  return true;
}

// Reads the six landmarks' x, y and z into `frame`'s fields, in
// FormatEyesLine's order. Returns whether every one was read.
bool ReadLandmarks(std::istringstream& row, EyesFrame& frame) {
  const std::array<std::array<double, 3>*, 6> points = {
      &frame.iris_left,  &frame.iris_right, &frame.corner_33,
      &frame.corner_133, &frame.corner_362, &frame.corner_263};
  for (std::array<double, 3>* point : points) {
    if (!(row >> point->at(0) >> point->at(1) >> point->at(2))) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<EyesFrame> ParseEyesLine(const std::string& line) {
  std::istringstream row(line);
  std::string kind;
  row >> kind;
  EyesFrame frame;
  std::string extra;
  if (kind != "eyes" || !(row >> frame.timestamp_ms) ||
      !ReadScores(row, frame) || !ReadLandmarks(row, frame) || row >> extra) {
    return std::nullopt;
  }
  return frame;
}

bool ReplayEyes(std::istream& input, std::vector<EyesFrame>& frames) {
  std::int64_t last = -1;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string kind;
    row >> kind;
    if (kind != "eyes") {
      continue;
    }
    const std::optional<EyesFrame> frame = ParseEyesLine(line);
    if (!frame || frame->timestamp_ms <= last) {
      return false;
    }
    last = frame->timestamp_ms;
    frames.push_back(*frame);
  }
  return !input.bad();
}

}  // namespace facepalm
