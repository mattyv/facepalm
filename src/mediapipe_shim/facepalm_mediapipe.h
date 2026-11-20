/* facepalm's C interface to MediaPipe's face landmarker and short-range face
 * detector, built by Bazel as
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
  FP_POSE_VALUES = 16, /* A 4x4 matrix. */
  FP_KEYPOINTS = 6     /* The face detector's key points. */
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
 * pixels are read in place, during the call only: MediaPipe holds no
 * reference to them once it returns, so the caller may then reuse the buffer
 * (checked on #20 by making each frame's buffer unreadable as detect
 * returned, over 50 frames). timestamp_ms must increase from call to call.
 * Returns 1 and fills out when it finds a face, 0 when it finds none, and -1
 * with why in error when it fails. Calls on one model must not overlap. */
int fp_face_model_detect(FpFaceModel* model, const uint8_t* rgba, int width,
                         int height, int64_t timestamp_ms, FpFace* out,
                         char* error, size_t error_size);

/* Closes the model. Not while a detect on it is running. */
void fp_face_model_destroy(FpFaceModel* model);

typedef struct FpFaceDetector FpFaceDetector;

/* One face as MediaPipe's legacy short-range face detection graph (BlazeFace)
 * finds it, as mediapipe.solutions.face_detection gives it, normalised to the
 * image: the box's left, top, width and height, and six key
 * points in the model's order: right eye, left eye, nose tip, mouth centre,
 * right ear tragion, left ear tragion (the subject's right and left).
 * Nodding Pigeon's inputs (#34). */
typedef struct {
  float box[4];
  float keypoints[FP_KEYPOINTS][2];
} FpDetection;

/* Loads the short-range face detector's .tflite into that graph, with a
 * minimum detection confidence of 0.5, as Nodding Pigeon runs it. Returns 0 and
 * sets *out, or -1 and writes why to error. */
int fp_face_detector_create(const char* model_path, FpFaceDetector** out,
                            char* error, size_t error_size);

/* Finds faces in one frame of packed RGBA, read during the call only.
 * Returns how many it found, filling out with the first, or -1 with why in
 * error when it fails. Calls on one detector must not overlap. */
int fp_face_detector_detect(FpFaceDetector* detector, const uint8_t* rgba,
                            int width, int height, FpDetection* out,
                            char* error, size_t error_size);

/* Closes the detector. Not while a detect on it is running. */
void fp_face_detector_destroy(FpFaceDetector* detector);
/* NOLINTEND */

#ifdef __cplusplus
}
#endif

#endif /* FACEPALM_SRC_MEDIAPIPE_SHIM_FACEPALM_MEDIAPIPE_H_ */
