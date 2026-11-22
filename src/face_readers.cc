#include "src/face_readers.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "src/calibration.h"
#include "src/calibration_reader.h"
#include "src/camera_controller.h"
#include "src/config.h"
#include "src/expression_reader.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/gesture_reader.h"
#include "src/look_away.h"
#include "src/look_away_reader.h"
#include "src/screen_context_reader.h"

namespace facepalm {
namespace {

// Forwards to a ScreenContextReader MakeFaceReaders is given (#61 item 11),
// which outlives this: the reader itself is owned by the daemon, not by
// one call's readers.
class ScreenContextReaderForwarder : public FaceReader {
 public:
  explicit ScreenContextReaderForwarder(ScreenContextReader& target)
      : target_(target) {}
  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override {
    target_.OnFace(face, width, height, timestamp_ms, reactions);
  }
  void OnNoFace() override { target_.OnNoFace(); }

 private:
  ScreenContextReader& target_;
};

// As ScreenContextReaderForwarder, for a LookAwayReader MakeFaceReaders is
// given (#141): outlives this, owned by the daemon.
class LookAwayReaderForwarder : public FaceReader {
 public:
  explicit LookAwayReaderForwarder(LookAwayReader& target) : target_(target) {}
  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override {
    target_.OnFace(face, width, height, timestamp_ms, reactions);
  }
  void OnNoFace() override { target_.OnNoFace(); }

 private:
  LookAwayReader& target_;
};

}  // namespace

std::vector<std::unique_ptr<FaceReader>> MakeFaceReaders(
    const Config& config, ScreenContextReader* screen_context_reader,
    CalibrationController* calibration, LookAwayReader* look_away_reader) {
  const ExpressionTracker::Settings tracker{
      .rise = config.expressions_smallest_rise,
      .pose_degrees = config.expressions_pose_degrees,
      .pose_range = config.expressions_pose_range,
      .settle_ms = config.expressions_settle_ms,
      .spreads = config.expressions_spreads,
      .hold_ms = config.expressions_hold_ms,
      .eye_look_down_threshold = config.expressions_eye_look_down_threshold,
      .eye_blink_threshold = config.expressions_eye_blink_threshold};
  std::vector<std::unique_ptr<FaceReader>> readers;
  if (config.gestures_enabled) {
    readers.push_back(
        std::make_unique<GestureReader>(config.gesture_confidence_threshold));
  }
  if (config.expressions_enabled) {
    readers.push_back(std::make_unique<ExpressionReader>(tracker));
  }
  if (config.looking_away_enabled) {
    if (look_away_reader != nullptr) {
      readers.push_back(
          std::make_unique<LookAwayReaderForwarder>(*look_away_reader));
    } else {
      readers.push_back(std::make_unique<LookAwayReader>(
          tracker,
          EyeGazeTracker::Settings{
              .blink_threshold = config.eye_gaze_blink_threshold,
              .median_ms = config.eye_gaze_median_ms,
              .resting_ms = config.eye_gaze_resting_ms,
              .gap_ms = config.eye_gaze_gap_ms},
          LookAwayDetector::Settings{
              .degrees = config.looking_away_degrees,
              .hold_ms = config.looking_away_hold_ms,
              .averted_ms = config.looking_away_averted_ms,
              .other_screen_margin_degrees =
                  config.looking_away_other_screen_margin_degrees},
          EyeGains{.horizontal = config.eye_gaze_horizontal_gain,
                   .vertical = config.eye_gaze_vertical_gain},
          config.eye_gaze_head_only_yaw_degrees));
    }
  }
  if (config.screen_context_enabled && screen_context_reader != nullptr) {
    readers.push_back(
        std::make_unique<ScreenContextReaderForwarder>(*screen_context_reader));
  }
  if (calibration != nullptr) {
    readers.push_back(std::make_unique<CalibrationReader>(
        *calibration, config.eye_gaze_blink_threshold));
  }
  return readers;
}

}  // namespace facepalm
