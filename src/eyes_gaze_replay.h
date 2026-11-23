#ifndef FACEPALM_SRC_EYES_GAZE_REPLAY_H_
#define FACEPALM_SRC_EYES_GAZE_REPLAY_H_

#include <iosfwd>

#include "src/config.h"

namespace facepalm {

// Replays camera_probe's head_pose, eyes and gesture_no_face lines
// together (#139, #140, #141): head pose composed with the real eye
// signal (src/eye_gaze.h's ComposeGaze), run through ExpressionTracker,
// EyeGazeTracker and LookAwayDetector in the same order LookAwayReader
// does (src/look_away_reader.cc) -- unlike ReplayLookingAway
// (src/look_away_replay.h), which reads head pose alone and gives the eye
// tracker nothing. Used to prove #139's fixtures (test/fixtures/eyes)
// against the tuned defaults, and to tune them.
//
// Each head_pose line must be immediately followed by an eyes line at
// the same timestamp, the way camera_probe always pairs them; anything
// else, a malformed line, or a timestamp that does not increase, returns
// false. gesture_no_face is #141's kGone, read as ReplayLookingAway reads
// it. Other lines are skipped.
//
// `config`'s settings are used for every tracker and the detector, its
// defaults when omitted. For each paired frame, prints
// `gaze <timestamp_ms> <yaw_degrees> <pitch_degrees>` (ComposeGaze's own
// result); for each expression ExpressionTracker::Update starts, prints
// `expression <timestamp_ms> <name>`; for each look away
// LookAwayDetector reports, prints
// `looked_away <timestamp_ms> <kind> <duration_ms>`, as ReplayLookingAway
// does; for each gesture_no_face line, prints `no_face <timestamp_ms>`
// (the last paired frame's own timestamp, the way LookAwayReader::OnNoFace
// has none of its own). Returns false, too, for an output error.
bool ReplayEyesAndLookAway(std::istream& input, std::ostream& output,
                           const Config& config = Config{});

}  // namespace facepalm

#endif  // FACEPALM_SRC_EYES_GAZE_REPLAY_H_
