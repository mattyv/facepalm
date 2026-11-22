#ifndef FACEPALM_SRC_EYES_PROBE_H_
#define FACEPALM_SRC_EYES_PROBE_H_

#include <cstdint>
#include <string>

#include "src/face_model.h"

namespace facepalm {

// Formats one camera_probe `eyes` line (#139), for fixtures #140 through
// #145 read: `eyes <timestamp_ms>` then, numbers only, the eye-look
// blendshapes (eyeLookDown, In, Out and Up, each left eye then right),
// eyeBlink, eyeSquint and eyeWide, each left then right; then the iris
// centres (landmarks 468, 473) and the eye corners (33, 133, 362, 263),
// each as its landmark's x, y and z. No trailing newline. Printed
// alongside `head_pose`, under the same degenerate-pose skip
// (IsFiniteRotation), since camera_probe.cc already holds the rotation
// when it decides whether to print that line.
std::string FormatEyesLine(const Face& face, std::int64_t timestamp_ms);

}  // namespace facepalm

#endif  // FACEPALM_SRC_EYES_PROBE_H_
