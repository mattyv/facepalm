#include "src/face_readers.h"

#include <memory>
#include <vector>

#include "src/config.h"
#include "src/expression_reader.h"
#include "src/expressions.h"
#include "src/face_reader.h"
#include "src/gesture_reader.h"
#include "src/look_away.h"
#include "src/look_away_reader.h"

namespace facepalm {

std::vector<std::unique_ptr<FaceReader>> MakeFaceReaders(const Config& config) {
  const ExpressionTracker::Settings tracker{
      .rise = config.expressions_smallest_rise,
      .pose_degrees = config.expressions_pose_degrees,
      .pose_range = config.expressions_pose_range,
      .settle_ms = config.expressions_settle_ms,
      .spreads = config.expressions_spreads,
      .hold_ms = config.expressions_hold_ms};
  std::vector<std::unique_ptr<FaceReader>> readers;
  if (config.gestures_enabled) {
    readers.push_back(
        std::make_unique<GestureReader>(config.gesture_confidence_threshold));
  }
  if (config.expressions_enabled) {
    readers.push_back(std::make_unique<ExpressionReader>(tracker));
  }
  if (config.looking_away_enabled) {
    readers.push_back(std::make_unique<LookAwayReader>(
        tracker,
        LookAwayDetector::Settings{.degrees = config.looking_away_degrees,
                                   .hold_ms = config.looking_away_hold_ms}));
  }
  return readers;
}

}  // namespace facepalm
