#ifndef FACEPALM_SRC_LOOK_AWAY_H_
#define FACEPALM_SRC_LOOK_AWAY_H_

#include <cstdint>
#include <optional>

#include "src/head_pose.h"

namespace facepalm {

// Finds where the person looks away (#35), in a stream of one face's head
// pose, against the resting pose ExpressionTracker learns: the head is away
// once its yaw or pitch is more than Settings::degrees from the resting
// pose, and looking away starts once it has stayed away for
// Settings::hold_ms. A nod or a shake leaves the resting pose for less than
// that, so neither is reported. Roll is a tilt, not a look away, and is not
// read. specs/LookAway.tla models the rule.
class LookAwayDetector {
 public:
  // Their defaults live in Config.
  struct Settings {
    // How far yaw or pitch must be from the resting pose to be away.
    double degrees;
    // How long the head must stay away.
    std::int64_t hold_ms;
  };

  explicit LookAwayDetector(Settings settings);

  // Takes the next frame's pose, the resting pose (nothing while it is
  // still being learned) and the frame's timestamp, which must increase
  // from call to call; returns whether looking away started on this frame.
  // It is reported once a look: the next is reported only after the head
  // comes back within half of Settings::degrees.
  bool Update(const HeadRotation& pose,
              const std::optional<HeadRotation>& resting,
              std::int64_t timestamp_ms);

 private:
  Settings settings_;
  bool away_ = false;         // Whether the head was away on the last frame.
  std::int64_t away_ms_ = 0;  // When it last went away.
  bool shown_ = false;        // Whether this look was reported.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LOOK_AWAY_H_
