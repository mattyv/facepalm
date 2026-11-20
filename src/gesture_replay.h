#ifndef FACEPALM_SRC_GESTURE_REPLAY_H_
#define FACEPALM_SRC_GESTURE_REPLAY_H_

#include <iosfwd>

namespace facepalm {

// Replays camera_probe's gesture_raw and gesture_no_face lines.
bool ReplayGestures(std::istream& input, std::ostream& output);

}  // namespace facepalm

#endif  // FACEPALM_SRC_GESTURE_REPLAY_H_
