// SPDX-FileCopyrightText: 2024 Idiap Research Institute <contact@idiap.ch>
// SPDX-License-Identifier: GPL-3.0-only
//
// Idiap's CCDbHG head-gesture model (idiap/ccdbhg-head-gesture-recognition),
// the cnn_lmk_hp checkpoint, ported from src/inference.py,
// src/classifier/classifier_cnn.py, src/classifier/base.py, src/utils.py
// and src/utils_dataset.py. Vuillecard, P. et al., "CCDb-HG: Novel
// Annotations and Gaze-Aware Representations for Head Gesture Recognition",
// IEEE FG 2024. Replaces Nodding Pigeon (#34).

#ifndef FACEPALM_SRC_HEAD_GESTURE_MODEL_H_
#define FACEPALM_SRC_HEAD_GESTURE_MODEL_H_

#include <array>
#include <cstdint>
#include <deque>
#include <optional>

#include "src/head_pose.h"
#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm::head_gesture {

inline constexpr int kWindowFrames = 31;  // config.yaml's max_len.
inline constexpr int kRawChannels = 18;   // 15 landmarks + 3 head pose.
inline constexpr int kLag = 5;            // config.yaml's transform_relative.
inline constexpr int kClasses = 6;  // none, nod, shake, tilt, turn, up/down.
// time_interpolation's time_of_window, for 31 frames at its ~30 fps training
// rate (33.333ms a frame): 33.333 * (31 - 1).
inline constexpr double kWindowDurationMs = 33.333 * (kWindowFrames - 1);
inline constexpr int kMinTrackFrames = 50;  // utils_demo.py's min_track_len.

// One frame's raw channels: the five points src/utils.py's get_landmarks
// averages from MediaPipe's mesh (left eye, right eye, nose, left ear,
// right ear; x, y, z each), then yaw, roll and pitch in radians, in Idiap's
// own convention (get_head_pose).
using RawFrame = std::array<float, kRawChannels>;

// get_landmarks (averaging) and get_head_pose (the facial transformation
// matrix's own convention), from one face and the frame's pixel size.
// `width` and `height` are needed because FpFace's landmarks are normalised
// per axis to the image, which get_landmarks' 3-D geometry needs in
// isotropic units; Idiap's own code works from a square face crop, where
// that holds automatically (#34 design note: FaceReader::OnFace carries the
// frame's size for this).
RawFrame RawFrameFrom(const FpFace& face, int width, int height);

// A closed-form Tait-Bryan "xyz" decomposition matching
// scipy.spatial.transform.Rotation.as_euler("xyz", degrees=False): the
// angles (a, b, c) such that rotation = Rz(c) * Ry(b) * Rx(a). At gimbal
// lock (b = +-90 degrees, where a and c are not separately observable) c is
// 0 and a holds their sum or difference, as scipy's own implementation does.
std::array<double, 3> EulerXyz(const Rotation& rotation);

// Idiap's src/utils.py get_head_pose: `rotation`'s yaw, roll and pitch, in
// radians, in its own convention (a coordinate flip of MediaPipe's canonical
// face - +X the subject's left, +Y up, +Z out of the face - then EulerXyz).
std::array<double, 3> IdiapHeadPoseFromRotation(const Rotation& rotation);

// What the model gives for a 31-frame window: none, nod, shake, tilt, turn,
// up/down, in that order (src/inference.py's label_name).
struct Prediction {
  std::array<float, kClasses> probabilities{};
};

// A window's features after HeadGestureInference.preprocess (normalised,
// relative-invariant, divided by the training std): 18 channels a frame,
// oldest first, the shape CNNModel.forward reads (once transposed).
using NormalizedFeatures = std::array<RawFrame, kWindowFrames>;

// CNNModel.forward (three Conv1d+ReLU stages, average pooling, the linear
// head) then softmax, on already-normalised features. Exposed on its own so
// the CNN's math is pinned independently of feature extraction (#34).
Prediction PredictFromNormalizedFeatures(const NormalizedFeatures& features);

// HeadGestureInference.preprocess (src/utils.py normalize_landmarks and
// src/utils_dataset.py's relative-invariant transform for both the head-pose
// and landmarks modalities, then division by the training std) followed by
// PredictFromNormalizedFeatures, on a 31-frame window, oldest first, already
// resampled to the model's training rate.
Prediction Predict(const std::array<RawFrame, kWindowFrames>& frames);

enum class Gesture : std::uint8_t {
  kNone,
  kNod,
  kShake,
  kTilt,
  kTurn,
  kUpDown
};

// The most likely class, or kNone when its probability is under
// `confidence_threshold`. Idiap's own inference.py get_data has no
// threshold (a bare argmax); facepalm adds one, as Nodding Pigeon's
// gesture_threshold did, to hold back a call the model is not sure of
// (#34). nan never compares >=, so it reads as kNone.
Gesture Classify(const Prediction& prediction, float confidence_threshold);

// Buffers faces at their own timestamps and resamples the last second of
// them onto the model's 31-frame, ~30 fps training grid, as Idiap's live
// demo does (src/utils_demo.py HGPredictor.preprocess, built on
// src/utils.py's time_interpolation): kMinTrackFrames buffered frames,
// spanning more than kWindowDurationMs, same as their own two gates
// (`len_track < min_track_len` and time_interpolation's span check).
//
// Idiap's live demo reads only the newest kMinTrackFrames frames, whose
// span falls under kWindowDurationMs from 50 fps up, where its own
// time_interpolation raises; Add instead keeps buffering older frames until
// the span is reached, reporting "not ready yet" meanwhile, so gestures
// still work at the configured ceiling of 60 fps (#34). At 49 fps and below
// the buffer trims to the newest kMinTrackFrames, exactly their demo's
// track.
class Window {
 public:
  // Adds `frame` at `timestamp_ms` (must increase from call to call), and
  // returns the resampled window once there is enough history.
  std::optional<std::array<RawFrame, kWindowFrames>> Add(
      const RawFrame& frame, std::int64_t timestamp_ms);

  // Drops all buffered frames: no face, so any window in progress is stale.
  void Reset();

 private:
  struct Entry {
    std::int64_t timestamp_ms = 0;
    RawFrame frame{};
  };

  std::deque<Entry> track_;
};

}  // namespace facepalm::head_gesture

#endif  // FACEPALM_SRC_HEAD_GESTURE_MODEL_H_
