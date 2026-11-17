#include "src/nodding_pigeon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

#include "src/nodding_pigeon_weights.h"

namespace facepalm::nodding_pigeon {
namespace {

constexpr int kPoints = 6;
constexpr int kBoxLeft = 0;
constexpr int kBoxTop = 1;
constexpr int kBoxWidth = 2;
constexpr int kBoxHeight = 3;
constexpr int kFirstX = 4;
constexpr int kFirstY = kFirstX + kPoints;
constexpr int kStdX = 2 * kPoints;
constexpr int kStdY = kStdX + 1;

// layers.Conv1D(1D, "valid" padding, then ReLU) over `in`, whose rows are
// time steps of `kChannels` values. The kernel is [position][channel][filter].
template <std::size_t kSteps, std::size_t kChannels, std::size_t kFilters,
          std::size_t kWidth, std::size_t kStride>
std::array<std::array<float, kFilters>, ((kSteps - kWidth) / kStride) + 1>
Conv1dRelu(const std::array<std::array<float, kChannels>, kSteps>& in,
           const std::array<float, kWidth * kChannels * kFilters>& kernel,
           const std::array<float, kFilters>& bias) {
  std::array<std::array<float, kFilters>, ((kSteps - kWidth) / kStride) + 1>
      out{};
  for (std::size_t t = 0; t < out.size(); ++t) {
    for (std::size_t f = 0; f < kFilters; ++f) {
      float sum = bias[f];
      for (std::size_t k = 0; k < kWidth; ++k) {
        for (std::size_t c = 0; c < kChannels; ++c) {
          sum += in[(t * kStride) + k][c] *
                 kernel[(((k * kChannels) + c) * kFilters) + f];
        }
      }
      out[t][f] = std::max(sum, 0.0F);
    }
  }
  return out;
}

// The population standard deviation of `values`, as tf.math.reduce_std.
float StandardDeviation(const std::array<float, kPoints>& values) {
  float mean = 0;
  for (const float value : values) {
    mean += value;
  }
  mean /= kPoints;
  float variance = 0;
  for (const float value : values) {
    variance += (value - mean) * (value - mean);
  }
  return std::sqrt(variance / kPoints);
}

// Rounds to two decimal places, as np.round(features, decimals=2).
float Hundredths(float value) {
  constexpr float kHundred = 100;
  return std::nearbyint(value * kHundred) / kHundred;
}

}  // namespace

RawFrame RawFrameFrom(const FpDetection& detection) {
  RawFrame frame{};
  std::ranges::copy(detection.box, frame.begin());
  for (int i = 0; i < kPoints; ++i) {
    frame[kFirstX + i] = detection.keypoints[i][0];
    frame[kFirstY + i] = detection.keypoints[i][1];
  }
  return frame;
}

std::array<float, kFeatures> Preprocess(const RawFrame& frame) {
  std::array<float, kPoints> xs{};
  std::array<float, kPoints> ys{};
  for (int i = 0; i < kPoints; ++i) {
    xs[i] = (frame[kFirstX + i] - frame[kBoxLeft]) / frame[kBoxWidth];
    ys[i] = (frame[kFirstY + i] - frame[kBoxTop]) / frame[kBoxHeight];
  }
  std::array<float, kFeatures> features{};
  std::ranges::copy(xs, features.begin());
  std::ranges::copy(ys, features.begin() + kPoints);
  features[kStdX] = StandardDeviation(xs);
  features[kStdY] = StandardDeviation(ys);
  return features;
}

Prediction Predict(const std::array<RawFrame, kFrames>& frames) {
  std::array<std::array<float, kFeatures>, kFrames> features{};
  for (int i = 0; i < kFrames; ++i) {
    features[i] = Preprocess(frames[i]);
  }
  constexpr std::size_t kWidth1 = 10;
  constexpr std::size_t kWidth2 = 5;
  constexpr std::size_t kStride = 2;
  const auto first = Conv1dRelu<kFrames, kFeatures, 2, kWidth1, kStride>(
      features, kConv1Kernel, kConv1Bias);
  const auto second = Conv1dRelu<first.size(), 2, 1, kWidth2, kStride>(
      first, kConv2Kernel, kConv2Bias);
  static_assert(second.size() == kMotionKernel.size());
  float motion = kMotionBias[0];
  std::array<float, 2> logits = kGestureBias;
  for (std::size_t i = 0; i < second.size(); ++i) {
    motion += second[i][0] * kMotionKernel[i];
    logits[0] += second[i][0] * kGestureKernel[2 * i];
    logits[1] += second[i][0] * kGestureKernel[(2 * i) + 1];
  }
  const float top = std::max(logits[0], logits[1]);
  const float nodding = std::exp(logits[0] - top);
  const float turning = std::exp(logits[1] - top);
  return Prediction{.has_motion = 1 / (1 + std::exp(-motion)),
                    .gestures = {nodding / (nodding + turning),
                                 turning / (nodding + turning)}};
}

Gesture Classify(const Prediction& prediction, float motion_threshold,
                 float gesture_threshold) {
  if (prediction.has_motion < motion_threshold) {
    return Gesture::kStationary;
  }
  const bool nodding = prediction.gestures[0] >= prediction.gestures[1];
  if (std::max(prediction.gestures[0], prediction.gestures[1]) <
      gesture_threshold) {
    return Gesture::kUndefined;
  }
  return nodding ? Gesture::kNodding : Gesture::kTurning;
}

std::optional<std::array<RawFrame, kFrames>> Window::Add(
    const RawFrame& frame) {
  if (!frames_.empty() &&
      std::ranges::equal(frame, frames_.back(), {}, Hundredths, Hundredths)) {
    return std::nullopt;
  }
  frames_.push_back(frame);
  if (frames_.size() > kFrames) {
    frames_.pop_front();
  }
  if (frames_.size() < kFrames) {
    return std::nullopt;
  }
  std::array<RawFrame, kFrames> out{};
  std::ranges::copy(frames_, out.begin());
  return out;
}

std::optional<std::array<RawFrame, kFrames>> Window::Padded() const {
  if (frames_.empty()) {
    return std::nullopt;
  }
  std::array<RawFrame, kFrames> out{};
  std::ranges::fill(out, frames_.back());
  std::ranges::copy(frames_, out.begin());
  return out;
}

}  // namespace facepalm::nodding_pigeon
