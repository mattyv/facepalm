#include "src/gesture_reader.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/head_gesture_model.h"

namespace facepalm {
namespace {

using head_gesture::Gesture;

// The reaction for a gesture that just started, indexed by Gesture's own
// order. Idiap's "up/down" reads up_down, so a client need not parse a
// slash out of JSON. kNone is never looked up (OnFace never reports it),
// but holds its place so the array stays indexed by Gesture.
constexpr std::array<std::string_view, head_gesture::kClasses> kReactionJson = {
    "",
    R"({"gesture":"nod"})",
    R"({"gesture":"shake"})",
    R"({"gesture":"tilt"})",
    R"({"gesture":"turn"})",
    R"({"gesture":"up_down"})",
};

std::string_view ReactionFor(Gesture gesture) {
  return kReactionJson.at(static_cast<std::size_t>(gesture));
}

// Idiap's src/utils_metrics.py get_events drops any event of 6 frames or
// fewer at its 30 fps training rate: 6 * (1000 / 30) ms. A class must hold
// this long, continuously, before GestureReportGate::Update reports it
// (#34).
constexpr std::int64_t kConfirmMs = 200;

// Longer than every gap between flips seen in the live recording that
// opened #34 (60-730ms, shake/turn seven times in 1.9 seconds): 600ms would
// read the longest gap, 730ms, as the head going still and close the
// episode mid-movement, so still_ms is 750ms, with margin over the longest
// gap actually seen.
constexpr std::int64_t kStillMs = 750;
// The recorded subject returned within ten degrees of the starting pose (#34).
// The same ten degrees is how far a head may drift and still count as
// settled where it is (#62).
constexpr double kTurnReturnRadians = 10 * std::numbers::pi / 180;
// How long a head must hold still away from a turn's origin before the pose
// it settled in becomes the new origin and turns are reported again (#62).
// A glance away is commonly taken to last under two seconds, the limit
// NHTSA's visual-manual driver distraction guidelines (2013) set for a
// single glance; a hold past it is a new posture, and turning back from it
// is a turn of its own.
constexpr std::int64_t kSettledAwayMs = 2000;

// The distance between yaws `a` and `b`, in radians, across the wrap at pi.
double YawDistance(double a, double b) {
  return std::abs(std::remainder(a - b, 2 * std::numbers::pi));
}

}  // namespace

// TurnExcursion's Away, Return, Hold, Settle and SettleAway (#34, #62).
bool GestureReportGate::SuppressTurn(Gesture gesture, std::int64_t timestamp_ms,
                                     TurnPose pose) {
  if (!turn_latched_) {
    return false;
  }
  if (gesture != Gesture::kNone) {
    // Away: the model reads a gesture, so the head is not still.
    turn_settling_ = false;
    turn_holding_ = false;
    return gesture == Gesture::kTurn;
  }
  // Settle: back within ten degrees of the origin, and still for kStillMs.
  if (YawDistance(pose.yaw, turn_origin_) > kTurnReturnRadians) {
    turn_settling_ = false;
  } else if (!turn_settling_) {
    turn_settling_ = true;
    turn_settling_ms_ = timestamp_ms;
  }
  // Hold: still within ten degrees of where the head settled; a drift past
  // that starts the hold again from the new pose.
  if (!turn_holding_ ||
      YawDistance(pose.yaw, turn_hold_yaw_) > kTurnReturnRadians) {
    turn_holding_ = true;
    turn_hold_ms_ = timestamp_ms;
    turn_hold_yaw_ = pose.yaw;
  }
  const bool returned =
      turn_settling_ && timestamp_ms - turn_settling_ms_ >= kStillMs;
  // SettleAway: still for kSettledAwayMs, wherever the head settled.
  const bool settled_away = timestamp_ms - turn_hold_ms_ >= kSettledAwayMs;
  if (returned || settled_away) {
    turn_latched_ = false;
  }
  return false;
}

head_gesture::Gesture GestureReportGate::Update(head_gesture::Gesture gesture,
                                                std::int64_t timestamp_ms,
                                                TurnPose pose) {
  if (SuppressTurn(gesture, timestamp_ms, pose)) {
    is_still_ = false;
    has_candidate_ = false;
    return Gesture::kNone;
  }
  if (gesture == Gesture::kNone) {
    has_candidate_ = false;
    if (episode_open_) {
      if (!is_still_) {
        is_still_ = true;
        still_start_ms_ = timestamp_ms;
      } else if (timestamp_ms - still_start_ms_ >= kStillMs) {
        episode_open_ = false;
        is_still_ = false;
      }
    }
    return Gesture::kNone;
  }

  is_still_ = false;
  if (episode_open_) {
    return Gesture::kNone;
  }

  if (has_candidate_ && candidate_ == gesture) {
    if (timestamp_ms - candidate_start_ms_ >= kConfirmMs) {
      episode_open_ = true;
      if (gesture == Gesture::kTurn) {
        // TurnExcursion's Turn: each excursion's holds start afresh.
        turn_latched_ = true;
        turn_settling_ = false;
        turn_holding_ = false;
        turn_origin_ = candidate_origin_;
      }
      has_candidate_ = false;
      return gesture;
    }
    return Gesture::kNone;
  }

  has_candidate_ = true;
  candidate_ = gesture;
  candidate_origin_ = pose.window_start_yaw;
  candidate_start_ms_ = timestamp_ms;
  return Gesture::kNone;
}

void GestureReportGate::Reset() {
  episode_open_ = false;
  has_candidate_ = false;
  is_still_ = false;
  // Turn clears the holds when it next latches.
  turn_latched_ = false;
}

GestureReader::GestureReader(float confidence_threshold)
    : confidence_threshold_(confidence_threshold) {}

// `width` and `height` follow FaceReader::OnFace's established order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void GestureReader::OnFace(const Face& face, int width, int height,
                           std::int64_t timestamp_ms,
                           std::vector<Reaction>& reactions) {
  const head_gesture::RawFrame raw =
      head_gesture::RawFrameFrom(face, width, height);
  const std::optional<
      std::array<head_gesture::RawFrame, head_gesture::kWindowFrames>>
      window = window_.Add(raw, timestamp_ms);
  if (!window) {
    return;
  }
  const Gesture gesture = head_gesture::Classify(head_gesture::Predict(*window),
                                                 confidence_threshold_);
  const Gesture to_report = gate_.Update(
      gesture, timestamp_ms,
      {.yaw = raw.at(head_gesture::kYawChannel),
       .window_start_yaw = window->front().at(head_gesture::kYawChannel)});
  if (to_report != Gesture::kNone) {
    reactions.emplace_back(ReactionFor(to_report));
  }
}

void GestureReader::OnNoFace() {
  window_.Reset();
  gate_.Reset();
}

}  // namespace facepalm
