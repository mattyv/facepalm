#ifndef FACEPALM_SRC_EYES_REPLAY_H_
#define FACEPALM_SRC_EYES_REPLAY_H_

#include <array>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace facepalm {

// One camera_probe `eyes` line (#139), parsed: the eye-look, blink,
// squint and wide blendshapes, each left eye then right, then the iris
// centres and the eye corners, each as its landmark's x, y and z, in the
// order FormatEyesLine (src/eyes_probe.h) prints them. The iris centres
// are MediaPipe's own left and right (468, 473); the corners keep their
// landmark numbers rather than claim which is inner or outer, which #139
// does not say either.
struct EyesFrame {
  std::int64_t timestamp_ms = 0;
  std::array<double, 2> eye_look_down{};
  std::array<double, 2> eye_look_in{};
  std::array<double, 2> eye_look_out{};
  std::array<double, 2> eye_look_up{};
  std::array<double, 2> eye_blink{};
  std::array<double, 2> eye_squint{};
  std::array<double, 2> eye_wide{};
  std::array<double, 3> iris_left{};   // Landmark 468.
  std::array<double, 3> iris_right{};  // Landmark 473.
  std::array<double, 3> corner_33{};
  std::array<double, 3> corner_133{};
  std::array<double, 3> corner_362{};
  std::array<double, 3> corner_263{};
};

// Parses one `eyes` line's fields, the way FormatEyesLine wrote them.
// `line` must start with "eyes"; returns nullopt when it does not go on
// with exactly a timestamp and the 32 numbers FormatEyesLine writes.
std::optional<EyesFrame> ParseEyesLine(const std::string& line);

// Replays camera_probe's `eyes` lines into `frames`, in order, the way
// ReplayLookingAway (src/look_away_replay.h) replays its head_pose lines:
// so a later issue's eye reader (#140 through #145) can test against
// test/fixtures/eyes's recordings. Other lines are skipped. Returns false
// for a malformed eyes line, a timestamp that does not increase, or a
// read error.
bool ReplayEyes(std::istream& input, std::vector<EyesFrame>& frames);

}  // namespace facepalm

#endif  // FACEPALM_SRC_EYES_REPLAY_H_
