#ifndef FACEPALM_SRC_NODDING_PIGEON_H_
#define FACEPALM_SRC_NODDING_PIGEON_H_

#include <array>
#include <cstdint>
#include <deque>
#include <optional>

#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm::nodding_pigeon {

// Nodding Pigeon's head gesture model (bhky/nodding-pigeon, MIT, weights
// v0.1.0), ported from noddingpigeon/model.py and inference.py.

inline constexpr int kFrames = 60;       // Config.seq_length.
inline constexpr int kRawFeatures = 16;  // Config.num_original_features.
inline constexpr int kFeatures = 14;     // After Preprocessing.

// One frame as video.py collects it from MediaPipe's short-range face
// detector, normalised to the image: the face box's left, top, width and
// height; then the x of the right eye, left eye, nose tip, mouth centre,
// right ear tragion and left ear tragion; then their y, in the same order.
using RawFrame = std::array<float, kRawFeatures>;

// A frame from the face detector's first face, in RawFrame's order: the
// detector gives its key points in Nodding Pigeon's order.
RawFrame RawFrameFrom(const FpDetection& detection);

// What the model gives for 60 frames.
struct Prediction {
  float has_motion = 0;
  std::array<float, 2> gestures{};  // Nodding, then turning; they sum to 1.
};

enum class Gesture : std::uint8_t {
  kStationary,
  kNodding,
  kTurning,
  kUndefined
};

// Preprocessing.call: the key points relative to the face box, then the
// population standard deviation of the six x and of the six y.
std::array<float, kFeatures> Preprocess(const RawFrame& frame);

// The model on 60 consecutive frames, oldest first.
Prediction Predict(const std::array<RawFrame, kFrames>& frames);

// inference.postprocess: stationary when has_motion is under
// `motion_threshold`; otherwise the likelier gesture when its probability
// is at least `gesture_threshold`, else undefined.
Gesture Classify(const Prediction& prediction, float motion_threshold,
                 float gesture_threshold);

// The last 60 frames, collected as video.py does with
// drop_consecutive_duplicates: a frame equal to the one before it, both
// rounded to two decimal places, is not kept.
class Window {
 public:
  // Adds `frame`, and returns the 60 frames once there are that many.
  std::optional<std::array<RawFrame, kFrames>> Add(const RawFrame& frame);

  // The frames so far, padded to 60 by repeating the last, as video.py's
  // end_padding does for a short video; nothing when there are none.
  [[nodiscard]] std::optional<std::array<RawFrame, kFrames>> Padded() const;

 private:
  std::deque<RawFrame> frames_;
};

}  // namespace facepalm::nodding_pigeon

#endif  // FACEPALM_SRC_NODDING_PIGEON_H_
