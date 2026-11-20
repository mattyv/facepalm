#include "src/expressions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace facepalm {
namespace {

constexpr std::int64_t kLearningMs = 3000;
constexpr std::int64_t kGapMs = 2000;
constexpr double kMsPerSecond = 1000;
// How fast the resting face follows the scores: quickly while it is first
// learned, slowly afterwards, so that an expression held for a few seconds
// stays an expression and a new seat or new light is absorbed in a minute.
constexpr double kLearningSeconds = 1;
constexpr double kRestingSeconds = 20;
// How fast the resting spread follows the scores: faster than the level, so
// that a face that was still settling stops counting within seconds.
constexpr double kSpreadSeconds = 5;
// An expression showing has ended once its rise is under this much of what
// started it, so that one easing a little is not reported twice.
constexpr double kEndFraction = 0.5;

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

}  // namespace

std::string_view Name(Expression expression) {
  constexpr std::array<std::string_view, 5> kNames = {
      "brows_up", "smile", "lips_pressed", "brows_down", "eyes_narrowed"};
  return kNames.at(static_cast<std::size_t>(expression));
}

ExpressionTracker::ExpressionTracker(Settings settings) : settings_(settings) {}

void ExpressionTracker::StartOver(const Scores& scores,
                                  std::int64_t timestamp_ms) {
  started_ = true;
  started_ms_ = timestamp_ms;
  for (std::size_t index = 0; index < states_.size(); ++index) {
    states_.at(index) = State{.level = Signal(index, scores)};
  }
}

std::vector<Expression> ExpressionTracker::Update(const Scores& scores,
                                                  std::int64_t timestamp_ms) {
  std::vector<Expression> began;
  const std::int64_t since_last = timestamp_ms - last_ms_;
  last_ms_ = timestamp_ms;
  if (!started_ || since_last > kGapMs) {
    StartOver(scores, timestamp_ms);
    return began;
  }
  const bool learning = timestamp_ms - started_ms_ < kLearningMs;
  const double level_weight =
      Weight(since_last, learning ? kLearningSeconds : kRestingSeconds);
  const double spread_weight = Weight(since_last, kSpreadSeconds);
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

}  // namespace facepalm
