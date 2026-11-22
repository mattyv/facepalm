#include "src/eye_gaze.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "src/face_model.h"
#include "src/head_pose.h"

namespace facepalm {
namespace {

// src/eyes_probe.h's own MediaPipe blendshape indices, left eye then right.
constexpr int kEyeLookDownLeft = 11;
constexpr int kEyeLookDownRight = 12;
constexpr int kEyeLookInLeft = 13;
constexpr int kEyeLookInRight = 14;
constexpr int kEyeLookOutLeft = 15;
constexpr int kEyeLookOutRight = 16;
constexpr int kEyeLookUpLeft = 17;
constexpr int kEyeLookUpRight = 18;
constexpr int kEyeBlinkLeft = 9;
constexpr int kEyeBlinkRight = 10;

double Mean(float left, float right) {
  return (static_cast<double>(left) + static_cast<double>(right)) / 2;
}

}  // namespace

EyeSignal EyeSignalFrom(const Face& face, double blink_threshold) {
  const double out_minus_in_left =
      static_cast<double>(face.blendshapes[kEyeLookOutLeft]) -
      static_cast<double>(face.blendshapes[kEyeLookInLeft]);
  const double in_minus_out_right =
      static_cast<double>(face.blendshapes[kEyeLookInRight]) -
      static_cast<double>(face.blendshapes[kEyeLookOutRight]);
  const double down =
      Mean(face.blendshapes[kEyeLookDownLeft], face.blendshapes[kEyeLookDownRight]);
  const double up =
      Mean(face.blendshapes[kEyeLookUpLeft], face.blendshapes[kEyeLookUpRight]);
  const double blink =
      Mean(face.blendshapes[kEyeBlinkLeft], face.blendshapes[kEyeBlinkRight]);
  return EyeSignal{.horizontal = (out_minus_in_left + in_minus_out_right) / 2,
                   .vertical = down - up,
                   .blink = blink > blink_threshold};
}

HeadRotation ComposeGaze(const HeadRotation& head,
                         const std::optional<EyeSignal>& eye,
                         const std::optional<EyeSignal>& eye_resting,
                         const EyeGains& gains, double head_only_yaw_degrees) {
  if (!eye || !eye_resting || std::abs(head.yaw_degrees) > head_only_yaw_degrees) {
    return head;
  }
  return HeadRotation{
      .yaw_degrees =
          head.yaw_degrees +
          gains.horizontal * (eye->horizontal - eye_resting->horizontal),
      .pitch_degrees =
          head.pitch_degrees +
          gains.vertical * (eye->vertical - eye_resting->vertical),
      .roll_degrees = head.roll_degrees};
}

EyeGazeTracker::EyeGazeTracker(Settings settings) : settings_(settings) {}

void EyeGazeTracker::LearnResting(const EyeSignal& signal,
                                  std::int64_t timestamp_ms) {
  const bool gap = have_last_ && timestamp_ms - last_ms_ > settings_.gap_ms;
  if (!resting_ || gap) {
    resting_ = signal;
    resting_samples_ = 1;
    resting_started_ms_ = timestamp_ms;
    return;
  }
  if (timestamp_ms - resting_started_ms_ <= settings_.resting_ms) {
    ++resting_samples_;
    const double weight = 1.0 / resting_samples_;
    resting_->horizontal += (signal.horizontal - resting_->horizontal) * weight;
    resting_->vertical += (signal.vertical - resting_->vertical) * weight;
  }
}

void EyeGazeTracker::Update(const std::optional<Face>& face,
                            std::int64_t timestamp_ms) {
  if (!face) {
    recent_.clear();
    have_last_ = false;
    return;
  }
  const EyeSignal signal = EyeSignalFrom(*face, settings_.blink_threshold);
  const bool gap = have_last_ && timestamp_ms - last_ms_ > settings_.gap_ms;
  if (gap) {
    recent_.clear();
  }
  if (!signal.blink) {
    LearnResting(signal, timestamp_ms);
    recent_.emplace_back(timestamp_ms, signal);
  }
  while (!recent_.empty() &&
        timestamp_ms - recent_.front().first > settings_.median_ms) {
    recent_.pop_front();
  }
  last_ms_ = timestamp_ms;
  have_last_ = true;
}

std::optional<EyeSignal> EyeGazeTracker::Signal() const {
  if (recent_.empty() || !resting_) {
    return std::nullopt;
  }
  std::vector<double> horizontal;
  std::vector<double> vertical;
  horizontal.reserve(recent_.size());
  vertical.reserve(recent_.size());
  for (const auto& [timestamp_ms, signal] : recent_) {
    horizontal.push_back(signal.horizontal);
    vertical.push_back(signal.vertical);
  }
  const auto median = [](std::vector<double>& values) {
    const std::size_t middle = values.size() / 2;
    std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(middle));
    return values[middle];
  };
  return EyeSignal{.horizontal = median(horizontal),
                   .vertical = median(vertical),
                   .blink = false};
}

std::optional<EyeSignal> EyeGazeTracker::Resting() const {
  if (!resting_ || resting_samples_ < 1) {
    return std::nullopt;
  }
  // Settings::resting_ms must actually have elapsed since resting_started_ms_
  // for the level to be considered learned; last_ms_ is the newest sample
  // seen, which stands in for "now" the way ExpressionTracker's own
  // RestingPose does.
  if (!have_last_ || last_ms_ - resting_started_ms_ < settings_.resting_ms) {
    return std::nullopt;
  }
  return resting_;
}

namespace {

double Range(std::span<const double> values) {
  const auto [low, high] = std::ranges::minmax(values);
  return high - low;
}

// The least-squares slope of `ys` over `xs` (ys = slope * xs + intercept),
// or nullopt when `xs`'s own variance is too small to divide by safely.
std::optional<double> Slope(std::span<const double> xs,
                            std::span<const double> ys) {
  const auto n = static_cast<double>(xs.size());
  double mean_x = 0;
  double mean_y = 0;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    mean_x += xs[i] / n;
    mean_y += ys[i] / n;
  }
  double covariance = 0;
  double variance = 0;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    covariance += (xs[i] - mean_x) * (ys[i] - mean_y);
    variance += (xs[i] - mean_x) * (xs[i] - mean_x);
  }
  constexpr double kSmallestVariance = 1e-9;
  if (variance < kSmallestVariance) {
    return std::nullopt;
  }
  return covariance / variance;
}

// One axis's estimate: nullopt when `head`'s own range is under
// `min_axis_degrees`, otherwise the negated slope of head over eye
// (EstimateGains's own documented formula), or nullopt when Slope itself
// cannot divide.
std::optional<double> EstimateOneAxis(std::span<const double> head,
                                      std::span<const double> eye,
                                      double min_axis_degrees) {
  if (Range(head) < min_axis_degrees) {
    return std::nullopt;
  }
  const std::optional<double> slope = Slope(eye, head);
  if (!slope) {
    return std::nullopt;
  }
  return -*slope;
}

}  // namespace

std::optional<EstimatedGains> EstimateGains(std::span<const GainsSample> samples,
                                            double min_travel_degrees,
                                            double min_axis_degrees) {
  std::vector<double> head_yaw;
  std::vector<double> head_pitch;
  std::vector<double> eye_horizontal;
  std::vector<double> eye_vertical;
  head_yaw.reserve(samples.size());
  head_pitch.reserve(samples.size());
  eye_horizontal.reserve(samples.size());
  eye_vertical.reserve(samples.size());
  for (const GainsSample& sample : samples) {
    head_yaw.push_back(sample.head.yaw_degrees);
    head_pitch.push_back(sample.head.pitch_degrees);
    eye_horizontal.push_back(sample.eye.horizontal);
    eye_vertical.push_back(sample.eye.vertical);
  }
  if (head_yaw.size() < 2) {
    return std::nullopt;
  }
  const double travel = std::max(Range(head_yaw), Range(head_pitch));
  if (travel < min_travel_degrees) {
    return std::nullopt;
  }
  return EstimatedGains{
      .horizontal =
          EstimateOneAxis(head_yaw, eye_horizontal, min_axis_degrees),
      .vertical = EstimateOneAxis(head_pitch, eye_vertical, min_axis_degrees)};
}

}  // namespace facepalm
