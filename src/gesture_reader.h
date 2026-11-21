#ifndef FACEPALM_SRC_GESTURE_READER_H_
#define FACEPALM_SRC_GESTURE_READER_H_

#include <cstdint>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/head_gesture_model.h"

namespace facepalm {

// A classified frame and its timestamp decide when a gesture is reported:
// kept separate from the model (#34) so the rule can be driven directly,
// on chosen class sequences, in test/gesture_reader_test.cc, without
// needing the real model to produce a particular run of classes.
//
// A class must be the model's answer continuously for some minimum time
// before it is reported, which opens an episode; nothing is reported again,
// whatever the class, until the head has answered kNone continuously for a
// longer minimum time, which closes the episode. Both minimums are
// GestureReader.cc's own constants. A live recording showed the model
// alternating between two related classes inside one physical movement
// (shake/turn, nod/up_down), and flickering to kNone for a single frame
// mid-gesture; this rule survives both (5ad22cb's latch, which reported
// every flip and reset on one kNone frame, did not).
class GestureReportGate {
 public:
  struct TurnPose {
    double yaw = 0;
    double window_start_yaw = 0;
  };
  // Feeds one classified frame's answer at `timestamp_ms` (must increase
  // from call to call), and returns the gesture to report, or
  // head_gesture::Gesture::kNone when nothing should be reported now. Yaw
  // and the yaw at the start of the model window are radians. After a turn,
  // later turns are suppressed until the model has read none for 750 ms with
  // the face back within ten degrees of the estimated pre-turn yaw (#34), or
  // for 2,000 ms with the face within ten degrees of wherever it settled
  // (#62): a new posture becomes the origin, so the suppression is bounded.
  head_gesture::Gesture Update(head_gesture::Gesture gesture,
                               std::int64_t timestamp_ms, TurnPose pose);
  head_gesture::Gesture Update(head_gesture::Gesture gesture,
                               std::int64_t timestamp_ms) {
    return Update(gesture, timestamp_ms, TurnPose{});
  }

  // Drops any candidate in progress and closes any open episode: with no
  // face, neither can be trusted to continue.
  void Reset();

 private:
  bool SuppressTurn(head_gesture::Gesture gesture, std::int64_t timestamp_ms,
                    TurnPose pose);

  bool turn_latched_ = false;
  bool turn_settling_ = false;
  std::int64_t turn_settling_ms_ = 0;
  bool turn_holding_ = false;
  std::int64_t turn_hold_ms_ = 0;
  double turn_hold_yaw_ = 0;
  double turn_origin_ = 0;
  double candidate_origin_ = 0;
  bool episode_open_ = false;
  bool has_candidate_ = false;
  head_gesture::Gesture candidate_ = head_gesture::Gesture::kNone;
  std::int64_t candidate_start_ms_ = 0;
  bool is_still_ = false;
  std::int64_t still_start_ms_ = 0;
};

// Detects head gestures with Idiap's CCDbHG model (#34), replacing
// GestureAnalyzer and Nodding Pigeon: each face goes into a window of the
// last second, resampled to the model's training rate; the model runs each
// time the window is ready. What to report from the model's per-frame
// answers is GestureReportGate's rule, not this class's.
class GestureReader : public FaceReader {
 public:
  explicit GestureReader(float confidence_threshold);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  void OnNoFace() override;

 private:
  float confidence_threshold_;
  head_gesture::Window window_;
  GestureReportGate gate_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_GESTURE_READER_H_
