#ifndef FACEPALM_SRC_REACTION_H_
#define FACEPALM_SRC_REACTION_H_

#include <optional>

#include "src/camera_controller.h"
#include "src/face_model.h"

namespace facepalm {

// The reaction to one face: its head's yaw, pitch and roll in degrees, from
// the face's transformation matrix, as JSON text, such as
// {"yaw":12.5,"pitch":-3.0,"roll":0.4}. Signs follow HeadRotation: positive
// yaw is turning to the subject's left, positive pitch is looking down, and
// positive roll is tilting towards the subject's right shoulder. Returns
// nothing for a degenerate matrix, such as all zeros or one holding NaN or
// infinity, whose rotation is not a set of numbers (#44): its angles would
// be NaN, which JSON cannot hold, or meaningless.
std::optional<Reaction> ReactionFrom(const Face& face);

}  // namespace facepalm

#endif  // FACEPALM_SRC_REACTION_H_
