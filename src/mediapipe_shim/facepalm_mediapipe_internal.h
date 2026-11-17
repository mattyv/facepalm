// The shim's internals that its test reaches directly: a real model always
// returns a result of the shape facepalm_mediapipe.h promises, so only a
// made-up result can show what happens when one does not. Not exported.

#ifndef FACEPALM_SRC_MEDIAPIPE_SHIM_FACEPALM_MEDIAPIPE_INTERNAL_H_
#define FACEPALM_SRC_MEDIAPIPE_SHIM_FACEPALM_MEDIAPIPE_INTERNAL_H_

#include <cstddef>

#include "absl/status/status.h"
#include "facepalm_shim/facepalm_mediapipe.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker_result.h"

namespace facepalm_shim {

// Writes `status` into the caller's buffer, cut to fit, and returns -1.
int Fail(const absl::Status& status, char* error, size_t error_size);

// Copies the first face of `result` into `out` and returns 1, or returns -1
// with why in `error` when its shape is not the one facepalm_mediapipe.h
// promises.
int CopyFace(
    const mediapipe::tasks::vision::face_landmarker::FaceLandmarkerResult&
        result,
    FpFace& out, char* error, size_t error_size);

}  // namespace facepalm_shim

#endif  // FACEPALM_SRC_MEDIAPIPE_SHIM_FACEPALM_MEDIAPIPE_INTERNAL_H_
