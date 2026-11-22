#ifndef FACEPALM_SRC_LOOK_AWAY_REPLAY_H_
#define FACEPALM_SRC_LOOK_AWAY_REPLAY_H_

#include <iosfwd>

namespace facepalm {

// Replays camera_probe's head_pose lines, `head_pose <timestamp_ms> <yaw>
// <pitch> <roll>` in degrees, and its gesture_no_face lines, through
// LookAwayReader's resting pose and detector with Config's defaults (no
// eyes line is read, so gaze is head pose alone, src/eye_gaze.h's own
// fallback with no eye signal, and no calibration profile, so kOtherScreen
// never applies): prints `looked_away <timestamp_ms> <kind> <duration_ms>`
// for each look away it reports (#35, #141). Other lines are skipped.
// Returns false for a malformed head_pose line, a timestamp that does not
// increase, or an output error.
bool ReplayLookingAway(std::istream& input, std::ostream& output);

}  // namespace facepalm

#endif  // FACEPALM_SRC_LOOK_AWAY_REPLAY_H_
