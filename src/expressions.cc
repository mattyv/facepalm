#include "src/expressions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "src/head_pose.h"

namespace facepalm {
namespace {

constexpr std::int64_t kLearningMs = 3000;
constexpr std::int64_t kGapMs = 2000;
constexpr double kMsPerSecond = 1000;
// How fast the resting face and the resting pose follow their input: quickly
// while first learned, slowly afterwards, so that an expression held for a
// few seconds stays an expression and a new seat, new light or new posture
// is absorbed in a minute.
constexpr double kLearningSeconds = 1;
constexpr double kRestingSeconds = 20;
// How fast the resting spread follows the scores: faster than the level, so
// that a face that was still settling stops counting within seconds.
constexpr double kSpreadSeconds = 5;
// An expression showing has ended once its rise is under this much of what
// started it, so that one easing a little is not reported twice.
constexpr double kEndFraction = 0.5;
// The window a head's pose range (#46) is measured over.
constexpr std::int64_t kPoseRangeWindowMs = 500;

// MediaPipe's indices for the scores read here.
constexpr std::array<int, 3> kBrowsUp = {3, 4, 5};  // browInnerUp, OuterUp*.
constexpr std::array<int, 2> kSmile = {44, 45};     // mouthSmile*.
// mouthShrugLower: the score that moved most on pressed lips, and not on a
// smile, which lifts mouthPress* as far as pressing does (#46).
constexpr std::array<int, 1> kLipsPressed = {42};
// Apart, not as one: a small frown moves browDown* alone, and averaged with
// eyeSquint* its rise is lost (#46).
constexpr std::array<int, 2> kBrowsDown = {1, 2};       // browDown*.
constexpr std::array<int, 2> kEyesNarrowed = {19, 20};  // eyeSquint*.

// The scores each expression is read from, in Expression's order.
constexpr std::array<std::span<const int>, 5> kScoresOf = {
    kBrowsUp, kSmile, kLipsPressed, kBrowsDown, kEyesNarrowed};

// The mean of the scores `expression` is read from.
double Signal(std::size_t expression, const Scores& scores) {
  double sum = 0;
  for (const int index : kScoresOf.at(expression)) {
    sum += static_cast<double>(scores.at(static_cast<std::size_t>(index)));
  }
  return sum / static_cast<double>(kScoresOf.at(expression).size());
}

// An exponential moving average's weight for a step of `step_ms`, when it
// follows its input over `seconds`.
double Weight(std::int64_t step_ms, double seconds) {
  return 1 - std::exp(-static_cast<double>(step_ms) / kMsPerSecond / seconds);
}

// `pose`'s yaw, pitch and roll, in that order, so the gate can treat the
// three axes alike.
std::array<double, 3> AsArray(const HeadRotation& pose) {
  return {pose.yaw_degrees, pose.pitch_degrees, pose.roll_degrees};
}

}  // namespace

std::string_view Name(Expression expression) {
  constexpr std::array<std::string_view, 5> kNames = {
      "brows_up", "smile", "lips_pressed", "brows_down", "eyes_narrowed"};
  return kNames.at(static_cast<std::size_t>(expression));
}

ExpressionTracker::ExpressionTracker(Settings settings) : settings_(settings) {}

void ExpressionTracker::StartOver(const Scores& scores,
                                  const HeadRotation& pose,
                                  std::int64_t timestamp_ms) {
  started_ = true;
  started_ms_ = timestamp_ms;
  resting_pose_ = AsArray(pose);
  recent_poses_.clear();
  moved_ms_ = kNotRisen;
  gated_ = false;
  for (std::size_t index = 0; index < states_.size(); ++index) {
    states_.at(index) = State{.level = Signal(index, scores)};
  }
}

bool ExpressionTracker::Away(const HeadRotation& pose, double level_weight,
                             bool learning, std::int64_t timestamp_ms) {
  // The resting pose, and whether the head is away from it or has moved
  // recently, update on every frame, learning included, so that a posture
  // held for a while becomes the resting pose and reopens the gate.
  const std::array<double, 3> current_pose = AsArray(pose);
  recent_poses_.emplace_back(timestamp_ms, current_pose);
  while (recent_poses_.front().first < timestamp_ms - kPoseRangeWindowMs) {
    recent_poses_.pop_front();
  }
  double away_by = 0;
  double range = 0;
  for (std::size_t axis = 0; axis < current_pose.size(); ++axis) {
    away_by = std::max(
        away_by, std::abs(current_pose.at(axis) - resting_pose_.at(axis)));
    double lowest = current_pose.at(axis);
    double highest = current_pose.at(axis);
    for (const auto& recent : recent_poses_) {
      lowest = std::min(lowest, recent.second.at(axis));
      highest = std::max(highest, recent.second.at(axis));
    }
    range = std::max(range, highest - lowest);
    resting_pose_.at(axis) +=
        level_weight * (current_pose.at(axis) - resting_pose_.at(axis));
  }
  if (away_by > settings_.pose_degrees || range > settings_.pose_range) {
    moved_ms_ = timestamp_ms;
  }
  return !learning && moved_ms_ != kNotRisen &&
         timestamp_ms - moved_ms_ < settings_.settle_ms;
}

bool ExpressionTracker::PostureChangedWhileGated(bool away) {
  if (away) {
    if (!gated_) {
      gated_ = true;
      gated_pose_ = resting_pose_;
    }
    return false;
  }
  if (!gated_) {
    return false;
  }
  gated_ = false;
  for (std::size_t axis = 0; axis < resting_pose_.size(); ++axis) {
    if (std::abs(resting_pose_.at(axis) - gated_pose_.at(axis)) >
        settings_.pose_degrees) {
      return true;
    }
  }
  return false;
}

std::vector<Expression> ExpressionTracker::Update(const Scores& scores,
                                                  const HeadRotation& pose,
                                                  std::int64_t timestamp_ms) {
  std::vector<Expression> began;
  const std::int64_t since_last = timestamp_ms - last_ms_;
  last_ms_ = timestamp_ms;
  if (!started_ || since_last > kGapMs) {
    StartOver(scores, pose, timestamp_ms);
    return began;
  }
  const bool learning = timestamp_ms - started_ms_ < kLearningMs;
  const double level_weight =
      Weight(since_last, learning ? kLearningSeconds : kRestingSeconds);
  const double spread_weight = Weight(since_last, kSpreadSeconds);

  const bool away = Away(pose, level_weight, learning, timestamp_ms);
  if (PostureChangedWhileGated(away)) {
    // The gate reopened on a different resting pose than it shut on: the
    // posture itself changed, and the scores shifted with it (#46), so
    // what they sit at now is the new resting face, not a rise from the
    // levels frozen at the old posture. Learn it afresh, as after a gap.
    StartOver(scores, pose, timestamp_ms);
    return began;
  }
  if (away) {
    // The scores follow the pose, not the face: report nothing, and clear
    // any pending hold so a rise that straddles the movement restarts it.
    // Levels and spreads are frozen until the head settles.
    for (State& state : states_) {
      state.risen_ms = kNotRisen;
    }
    return began;
  }

  for (std::size_t index = 0; index < states_.size(); ++index) {
    State& state = states_.at(index);
    const double rise = Signal(index, scores) - state.level;
    const double starts_at =
        std::max(settings_.rise, settings_.spreads * state.spread);
    const bool risen =
        !learning && rise > (state.showing ? kEndFraction : 1) * starts_at;
    if (!risen) {
      state.showing = false;
      state.risen_ms = kNotRisen;
      // Only a face at rest says how far its scores wobble.
      state.spread += spread_weight * (std::abs(rise) - state.spread);
    } else if (!state.showing) {
      state.risen_ms =
          state.risen_ms == kNotRisen ? timestamp_ms : state.risen_ms;
      if (timestamp_ms - state.risen_ms >= settings_.hold_ms) {
        state.showing = true;
        began.push_back(static_cast<Expression>(index));
      }
    }
    state.level += level_weight * rise;
  }
  return began;
}

std::optional<HeadRotation> ExpressionTracker::RestingPose() const {
  if (!started_ || last_ms_ - started_ms_ < kLearningMs) {
    return std::nullopt;
  }
  return HeadRotation{.yaw_degrees = resting_pose_.at(0),
                      .pitch_degrees = resting_pose_.at(1),
                      .roll_degrees = resting_pose_.at(2)};
}

}  // namespace facepalm
