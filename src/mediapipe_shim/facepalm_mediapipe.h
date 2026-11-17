/* facepalm's C interface to MediaPipe's face landmarker, built by Bazel as
 * libfacepalm_mediapipe.dylib (src/mediapipe_shim/BUILD). Plain C, so that
 * facepalm's CMake build uses it without MediaPipe's headers, abseil or C++
 * runtime, and nothing allocated on one side is freed on the other. MediaPipe's
 * own mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h is the
 * reference. */

#ifndef FACEPALM_SRC_MEDIAPIPE_SHIM_FACEPALM_MEDIAPIPE_H_
#define FACEPALM_SRC_MEDIAPIPE_SHIM_FACEPALM_MEDIAPIPE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Plain C on purpose, for the ABI, so C++ style checks do not apply. */
/* NOLINTBEGIN */
enum {
  FP_LANDMARKS = 478,  /* MediaPipe's face mesh, with irises. */
  FP_BLENDSHAPES = 52, /* In MediaPipe's order, starting with _neutral. */
  FP_POSE_VALUES = 16  /* A 4x4 matrix. */
};

typedef struct FpFaceModel FpFaceModel;

/* One face. Landmarks are x and y normalised to the image, and z on x's
 * scale. The pose is MediaPipe's facial transformation matrix, column by
 * column (src/head_pose.h reads it). */
typedef struct {
  float landmarks[FP_LANDMARKS][3];
  float blendshapes[FP_BLENDSHAPES];
  float pose_column_major[FP_POSE_VALUES];
} FpFace;

/* Loads the face landmarker's .task bundle, in MediaPipe's VIDEO running
 * mode: one face, tracked from frame to frame, with landmarks smoothed.
 * Returns 0 and sets *out, or -1 and writes why to error. */
int fp_face_model_create(const char* model_path, FpFaceModel** out, char* error,
                         size_t error_size);

/* Finds the face in one frame of packed RGBA, width * height * 4 bytes. The
 * pixels are read in place, and must stay valid until the next detect or
 * destroy. timestamp_ms must increase from call to call. Returns 1 and fills
 * out when it finds a face, 0 when it finds none, and -1 with why in error
 * when it fails. Calls on one model must not overlap. */
int fp_face_model_detect(FpFaceModel* model, const uint8_t* rgba, int width,
                         int height, int64_t timestamp_ms, FpFace* out,
                         char* error, size_t error_size);

/* Closes the model. Not while a detect on it is running. */
void fp_face_model_destroy(FpFaceModel* model);
/* NOLINTEND */

#ifdef __cplusplus
}
#endif

#endif /* FACEPALM_SRC_MEDIAPIPE_SHIM_FACEPALM_MEDIAPIPE_H_ */
