// SPDX-FileCopyrightText: 2024 Idiap Research Institute <contact@idiap.ch>
// SPDX-License-Identifier: GPL-3.0-only

#include "src/head_gesture_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>

#include "src/head_gesture_model_weights.h"
#include "src/head_pose.h"

namespace facepalm::head_gesture {
namespace {

// src/utils.py's landmark groups: indices into MediaPipe's 468-point face
// mesh, which are FpFace's first 468 landmarks (the last 10 are the irises
// MediaPipe appends, which Idiap's model does not use).
constexpr std::array<int, 16> kLeftEye = {130, 247, 30, 29, 27, 28, 56,  190,
                                          243, 112, 26, 22, 23, 24, 110, 25};
constexpr std::array<int, 16> kRightEye = {463, 414, 286, 258, 257, 259,
                                           260, 467, 359, 255, 339, 254,
                                           253, 252, 256, 341};
constexpr std::array<int, 15> kNose = {51,  5, 281, 45,  4,   275, 220, 134,
                                       236, 3, 195, 248, 456, 363, 440};
constexpr std::array<int, 4> kEarLeft = {234, 93, 227, 137};
constexpr std::array<int, 4> kEarRight = {454, 323, 447, 366};

constexpr int kPoints = 5;  // left eye, right eye, nose, left ear, right ear.
constexpr int kCoordinates = 3;                            // x, y, z.
constexpr int kLandmarkChannels = kPoints * kCoordinates;  // 15.
constexpr int kEarLeftPoint = 3;
constexpr int kEarRightPoint = 4;
constexpr int kEmbeddingChannels = 128;  // config.yaml's feat_dim.

// The mean of the points `face` names at `indices`, each scaled to pixels:
// x and z by `size.width` (z is on x's scale, as facepalm_mediapipe.h
// documents), y by `size.height`. Mirrors src/utils.py get_landmarks's
// per-group mean.
struct PixelSize {
  double width = 0;
  double height = 0;
};

template <std::size_t N>
std::array<double, kCoordinates> AveragePoint(const FpFace& face,
                                              const std::array<int, N>& indices,
                                              PixelSize size) {
  std::array<double, kCoordinates> sum{};
  for (const int index : indices) {
    sum[0] += static_cast<double>(face.landmarks[index][0]) * size.width;
    sum[1] += static_cast<double>(face.landmarks[index][1]) * size.height;
    sum[2] += static_cast<double>(face.landmarks[index][2]) * size.width;
  }
  for (double& value : sum) {
    value /= static_cast<double>(N);
  }
  return sum;
}

using Matrix3 = std::array<std::array<double, 3>, 3>;
using LandmarkChannels = std::array<double, kLandmarkChannels>;
using HeadPoseChannels = std::array<double, 3>;

// Rz(roll) * Ry(yaw) * Rx(pitch), as src/utils_dataset.py's
// euler_hp_to_rotation_matrix builds it (via
// R_.from_euler("xyz", [pitch, yaw, roll])).
Matrix3 MatrixFromPitchYawRoll(double pitch, double yaw, double roll) {
  const double ca = std::cos(pitch);
  const double sa = std::sin(pitch);
  const double cb = std::cos(yaw);
  const double sb = std::sin(yaw);
  const double cc = std::cos(roll);
  const double sc = std::sin(roll);
  return Matrix3{{
      {(cc * cb), (cc * sa * sb) - (sc * ca), (cc * ca * sb) + (sc * sa)},
      {(sc * cb), (sc * sa * sb) + (cc * ca), (sc * ca * sb) - (cc * sa)},
      {-sb, sa * cb, ca * cb},
  }};
}

Matrix3 Transpose(const Matrix3& m) {
  Matrix3 out{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      out.at(row).at(column) = m.at(column).at(row);
    }
  }
  return out;
}

Matrix3 Multiply(const Matrix3& a, const Matrix3& b) {
  Matrix3 out{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      double sum = 0;
      for (int k = 0; k < 3; ++k) {
        sum += a.at(row).at(k) * b.at(k).at(column);
      }
      out.at(row).at(column) = sum;
    }
  }
  return out;
}

std::array<double, 3> MultiplyVector(const Matrix3& m,
                                     const std::array<double, 3>& v) {
  std::array<double, 3> out{};
  for (int row = 0; row < 3; ++row) {
    out.at(row) = (m.at(row).at(0) * v.at(0)) + (m.at(row).at(1) * v.at(1)) +
                  (m.at(row).at(2) * v.at(2));
  }
  return out;
}

// src/utils_dataset.py's rotation_matrix_to_euler_hp: (yaw, roll, pitch)
// from a rotation built purely from pitch/yaw/roll algebra (no MediaPipe
// coordinate flip; contrast IdiapHeadPoseFromRotation).
std::array<double, 3> PitchYawRollDecode(const Matrix3& m) {
  const std::array<double, 3> abc = EulerXyz(Rotation{.rows = m});
  return {abc[1], abc[2], abc[0]};  // yaw, roll, pitch.
}

// The window's raw channels, split into the two modalities src/utils_dataset
// .py treats separately.
struct RawChannels {
  std::array<LandmarkChannels, kWindowFrames> landmarks{};
  std::array<HeadPoseChannels, kWindowFrames> head_pose{};  // yaw, roll, pitch.
};

RawChannels SplitRawChannels(
    const std::array<RawFrame, kWindowFrames>& frames) {
  RawChannels out;
  for (int t = 0; t < kWindowFrames; ++t) {
    for (int c = 0; c < kLandmarkChannels; ++c) {
      out.landmarks.at(t).at(c) = frames.at(t).at(c);
    }
    for (int c = 0; c < 3; ++c) {
      out.head_pose.at(t).at(c) = frames.at(t).at(kLandmarkChannels + c);
    }
  }
  return out;
}

// src/utils_dataset.py compute_relative_invariant's "head_pose" branch: the
// rotation from kLag frames ago to now, decoded back to yaw/roll/pitch.
std::array<HeadPoseChannels, kWindowFrames> HeadPoseRelative(
    const std::array<HeadPoseChannels, kWindowFrames>& head_pose_raw) {
  std::array<Matrix3, kWindowFrames> rotations{};
  for (int t = 0; t < kWindowFrames; ++t) {
    rotations.at(t) = MatrixFromPitchYawRoll(
        head_pose_raw.at(t)[2], head_pose_raw.at(t)[0], head_pose_raw.at(t)[1]);
  }
  std::array<HeadPoseChannels, kWindowFrames> relative{};
  for (int t = 0; t < kWindowFrames; ++t) {
    const int previous = std::max(t - kLag, 0);
    const Matrix3 step =
        Multiply(Transpose(rotations.at(t)), rotations.at(previous));
    relative.at(t) = PitchYawRollDecode(step);
  }
  return relative;
}

// src/utils.py normalize_landmarks: head size from the mean distance
// between the left- and right-ear points, over the window, /100.
double HeadSize(const std::array<LandmarkChannels, kWindowFrames>& land_raw) {
  double head_size = 0;
  for (int t = 0; t < kWindowFrames; ++t) {
    double squared = 0;
    for (int axis = 0; axis < kCoordinates; ++axis) {
      const double left =
          land_raw.at(t).at((kEarLeftPoint * kCoordinates) + axis);
      const double right =
          land_raw.at(t).at((kEarRightPoint * kCoordinates) + axis);
      squared += (left - right) * (left - right);
    }
    head_size += std::sqrt(squared);
  }
  constexpr double kHeadSizeScale = 100;
  return head_size / (static_cast<double>(kWindowFrames) * kHeadSizeScale);
}

std::array<LandmarkChannels, kWindowFrames> NormalizeLandmarks(
    const std::array<LandmarkChannels, kWindowFrames>& land_raw,
    double head_size) {
  std::array<LandmarkChannels, kWindowFrames> land_norm{};
  for (int t = 0; t < kWindowFrames; ++t) {
    for (int c = 0; c < kLandmarkChannels; ++c) {
      land_norm.at(t).at(c) = land_raw.at(t).at(c) / head_size;
    }
  }
  return land_norm;
}

// The window's average head rotation, src/utils_dataset.py's
// compute_avg_rotation_matrix.
Matrix3 AverageRotation(
    const std::array<HeadPoseChannels, kWindowFrames>& head_pose_raw) {
  HeadPoseChannels mean_pose{};
  for (const HeadPoseChannels& pose : head_pose_raw) {
    for (int c = 0; c < 3; ++c) {
      mean_pose.at(c) += pose.at(c);
    }
  }
  for (double& value : mean_pose) {
    value /= kWindowFrames;
  }
  return MatrixFromPitchYawRoll(mean_pose[2], mean_pose[0], mean_pose[1]);
}

// One frame's 5 head-centred landmark points, rotated into the window's
// average head orientation and re-centred (compute_relative_landmark
// _invariant's "with_head_center" method, before the kLag-frame difference).
LandmarkChannels RecenterFrame(const LandmarkChannels& land_norm,
                               const Matrix3& average_rotation_transpose) {
  std::array<double, kCoordinates> center{};
  for (int point = 0; point < kPoints; ++point) {
    for (int axis = 0; axis < kCoordinates; ++axis) {
      center.at(axis) += land_norm.at((point * kCoordinates) + axis);
    }
  }
  for (double& value : center) {
    value /= kPoints;
  }
  LandmarkChannels out{};
  for (int point = 0; point < kPoints; ++point) {
    std::array<double, kCoordinates> relative{};
    for (int axis = 0; axis < kCoordinates; ++axis) {
      relative.at(axis) =
          land_norm.at((point * kCoordinates) + axis) - center.at(axis);
    }
    const std::array<double, kCoordinates> rotated =
        MultiplyVector(average_rotation_transpose, relative);
    for (int axis = 0; axis < kCoordinates; ++axis) {
      out.at((point * kCoordinates) + axis) =
          rotated.at(axis) + center.at(axis);
    }
  }
  return out;
}

// src/utils_dataset.py compute_relative_landmark_invariant's
// "with_head_center" method, then compute_relative's kLag-frame difference.
std::array<LandmarkChannels, kWindowFrames> LandmarksRelative(
    const std::array<LandmarkChannels, kWindowFrames>& land_norm,
    const std::array<HeadPoseChannels, kWindowFrames>& head_pose_raw) {
  const Matrix3 average_rotation_transpose =
      Transpose(AverageRotation(head_pose_raw));
  std::array<LandmarkChannels, kWindowFrames> centered{};
  for (int t = 0; t < kWindowFrames; ++t) {
    centered.at(t) = RecenterFrame(land_norm.at(t), average_rotation_transpose);
  }
  std::array<LandmarkChannels, kWindowFrames> relative{};
  for (int t = 0; t < kWindowFrames; ++t) {
    const int previous = std::max(t - kLag, 0);
    for (int c = 0; c < kLandmarkChannels; ++c) {
      relative.at(t).at(c) = centered.at(t).at(c) - centered.at(previous).at(c);
    }
  }
  return relative;
}

// Concatenates (head_pose_relative, land_rel) and divides by the training
// std, as TS_Normalizer.normalize does with no mean (mean_method "none").
NormalizedFeatures BuildNormalizedFeatures(
    const std::array<HeadPoseChannels, kWindowFrames>& head_pose_relative,
    const std::array<LandmarkChannels, kWindowFrames>& land_rel) {
  NormalizedFeatures features{};
  for (int t = 0; t < kWindowFrames; ++t) {
    for (int c = 0; c < 3; ++c) {
      features.at(t).at(c) = static_cast<float>(
          head_pose_relative.at(t).at(c) /
          (kChannelStd.at(c) + std::numeric_limits<double>::epsilon()));
    }
    for (int c = 0; c < kLandmarkChannels; ++c) {
      features.at(t).at(3 + c) = static_cast<float>(
          land_rel.at(t).at(c) /
          (kChannelStd.at(3 + c) + std::numeric_limits<double>::epsilon()));
    }
  }
  return features;
}

// 1D convolution, stride 1, zero padding of K/2 on each side (as
// nn.Conv1d(..., padding=kernel_size//2) is configured, bias=false), then
// ReLU. `in` is [Tin][Cin]; `weight` is PyTorch's native
// [Cout][Cin][K] flattened.
template <std::size_t Tin, std::size_t Cin, std::size_t Cout, std::size_t K>
std::array<std::array<float, Cout>, Tin + (2 * (K / 2)) - K + 1> Conv1dRelu(
    const std::array<std::array<float, Cin>, Tin>& in,
    const std::array<float, Cout * Cin * K>& weight) {
  constexpr std::size_t kPad = K / 2;
  constexpr std::size_t kTout = Tin + (2 * kPad) - K + 1;
  std::array<std::array<float, Cout>, kTout> out{};
  for (std::size_t t = 0; t < kTout; ++t) {
    for (std::size_t o = 0; o < Cout; ++o) {
      float sum = 0;
      for (std::size_t k = 0; k < K; ++k) {
        const std::size_t padded = t + k;
        if (padded < kPad || padded >= kPad + Tin) {
          continue;  // Zero padding contributes nothing.
        }
        const std::size_t in_t = padded - kPad;
        for (std::size_t c = 0; c < Cin; ++c) {
          sum += in.at(in_t).at(c) * weight.at((((o * Cin) + c) * K) + k);
        }
      }
      out.at(t).at(o) = std::max(sum, 0.0F);
    }
  }
  return out;
}

template <std::size_t T, std::size_t C>
std::array<float, C> AveragePool(
    const std::array<std::array<float, C>, T>& in) {
  std::array<float, C> out{};
  for (std::size_t t = 0; t < T; ++t) {
    for (std::size_t c = 0; c < C; ++c) {
      out.at(c) += in.at(t).at(c);
    }
  }
  for (float& value : out) {
    value /= static_cast<float>(T);
  }
  return out;
}

std::array<float, kClasses> Softmax(const std::array<float, kClasses>& logits) {
  const float top = *std::ranges::max_element(logits);
  std::array<float, kClasses> exp{};
  float sum = 0;
  for (std::size_t i = 0; i < kClasses; ++i) {
    exp.at(i) = std::exp(logits.at(i) - top);
    sum += exp.at(i);
  }
  std::array<float, kClasses> out{};
  for (std::size_t i = 0; i < kClasses; ++i) {
    out.at(i) = exp.at(i) / sum;
  }
  return out;
}

}  // namespace

// `width` and `height` follow FrameAnalyzer::Analyze's established order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
RawFrame RawFrameFrom(const FpFace& face, int width, int height) {
  const PixelSize size{.width = static_cast<double>(width),
                       .height = static_cast<double>(height)};
  const std::array<std::array<double, kCoordinates>, kPoints> points = {
      AveragePoint(face, kLeftEye, size), AveragePoint(face, kRightEye, size),
      AveragePoint(face, kNose, size), AveragePoint(face, kEarLeft, size),
      AveragePoint(face, kEarRight, size)};
  RawFrame frame{};
  int i = 0;
  for (const auto& point : points) {
    for (const double value : point) {
      frame.at(i) = static_cast<float>(value);
      ++i;
    }
  }
  PoseMatrix pose{};
  std::ranges::copy(face.pose_column_major, pose.begin());
  const std::array<double, 3> yaw_roll_pitch =
      IdiapHeadPoseFromRotation(RotationFrom(pose));
  frame.at(kLandmarkChannels) = static_cast<float>(yaw_roll_pitch[0]);
  frame.at(kLandmarkChannels + 1) = static_cast<float>(yaw_roll_pitch[1]);
  frame.at(kLandmarkChannels + 2) = static_cast<float>(yaw_roll_pitch[2]);
  return frame;
}

std::array<double, 3> EulerXyz(const Rotation& rotation) {
  const auto& m = rotation.rows;
  const double clamped = std::clamp(-m[2][0], -1.0, 1.0);
  const double b = std::asin(clamped);
  // Within this distance of 1, m[2][0] is treated as exactly at gimbal
  // lock, as src/head_pose.h's HeadRotationFrom does.
  constexpr double kGimbalLockMargin = 1e-9;
  if (std::abs(m[2][0]) < 1 - kGimbalLockMargin) {
    const double a = std::atan2(m[2][1], m[2][2]);
    const double c = std::atan2(m[1][0], m[0][0]);
    return {a, b, c};
  }
  // Gimbal lock: only a-c (b = +90 degrees) or a+c (b = -90 degrees) is
  // observable, so scipy's as_euler("xyz") sets c to 0 and folds the sum
  // or difference into a.
  const double sign = m[2][0] <= 0 ? 1.0 : -1.0;
  const double a = std::atan2(sign * m[0][1], m[1][1]);
  return {a, b, 0.0};
}

std::array<double, 3> IdiapHeadPoseFromRotation(const Rotation& rotation) {
  // src/utils.py get_head_pose: re[1:3,:] = -re[1:3,:] then
  // rot_matrix = re[:3,:3] @ diag(1,-1,-1), which together conjugate the
  // rotation by D = diag(1,-1,-1): entries that touch exactly one of axes
  // 1 or 2 (not both, not neither) are negated.
  Matrix3 conjugated = rotation.rows;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      const bool row_flips = row == 1 || row == 2;
      const bool column_flips = column == 1 || column == 2;
      if (row_flips != column_flips) {
        conjugated.at(row).at(column) = -conjugated.at(row).at(column);
      }
    }
  }
  const std::array<double, 3> abc = EulerXyz(Rotation{.rows = conjugated});
  return {abc[1], abc[2], abc[0]};  // yaw, roll, pitch.
}

Prediction Predict(const std::array<RawFrame, kWindowFrames>& frames) {
  const RawChannels raw = SplitRawChannels(frames);
  const std::array<HeadPoseChannels, kWindowFrames> head_pose_relative =
      HeadPoseRelative(raw.head_pose);
  const std::array<LandmarkChannels, kWindowFrames> land_norm =
      NormalizeLandmarks(raw.landmarks, HeadSize(raw.landmarks));
  const std::array<LandmarkChannels, kWindowFrames> land_rel =
      LandmarksRelative(land_norm, raw.head_pose);
  return PredictFromNormalizedFeatures(
      BuildNormalizedFeatures(head_pose_relative, land_rel));
}

Prediction PredictFromNormalizedFeatures(const NormalizedFeatures& features) {
  const auto h1 =
      Conv1dRelu<kWindowFrames, kRawChannels, kEmbeddingChannels, 8>(
          features, kConv1Weight);
  const auto h2 =
      Conv1dRelu<h1.size(), kEmbeddingChannels, kEmbeddingChannels, 5>(
          h1, kConv2Weight);
  const auto h3 =
      Conv1dRelu<h2.size(), kEmbeddingChannels, kEmbeddingChannels, 3>(
          h2, kConv3Weight);
  const std::array<float, kEmbeddingChannels> embedding =
      AveragePool<h3.size(), kEmbeddingChannels>(h3);

  std::array<float, kClasses> logits = kHeadBias;
  for (int cls = 0; cls < kClasses; ++cls) {
    for (int o = 0; o < kEmbeddingChannels; ++o) {
      logits.at(cls) +=
          kHeadWeight.at((cls * kEmbeddingChannels) + o) * embedding.at(o);
    }
  }
  return Prediction{.probabilities = Softmax(logits)};
}

Gesture Classify(const Prediction& prediction, float confidence_threshold) {
  const auto* const best = std::ranges::max_element(prediction.probabilities);
  const auto index = std::distance(prediction.probabilities.begin(), best);
  if (!(*best >= confidence_threshold)) {
    return Gesture::kNone;
  }
  return static_cast<Gesture>(index);
}

std::optional<std::array<RawFrame, kWindowFrames>> Window::Add(
    const RawFrame& frame, std::int64_t timestamp_ms) {
  track_.push_back({.timestamp_ms = timestamp_ms, .frame = frame});
  if (track_.size() > kMinTrackFrames) {
    track_.pop_front();
  }
  if (track_.size() < kMinTrackFrames) {
    return std::nullopt;
  }
  const std::int64_t end = track_.back().timestamp_ms;
  // time_interpolation's own search: the earliest frame, from the end, more
  // than kWindowDurationMs older than `end`.
  std::optional<std::size_t> start;
  for (std::size_t i = track_.size(); i > 0; --i) {
    const std::size_t index = i - 1;
    if (static_cast<double>(end - track_.at(index).timestamp_ms) >
        kWindowDurationMs) {
      start = index;
      break;
    }
  }
  if (!start) {
    return std::nullopt;  // Not enough time span yet (#34 design note).
  }

  std::array<RawFrame, kWindowFrames> out{};
  std::size_t left = *start;
  for (int k = 0; k < kWindowFrames; ++k) {
    const double target =
        static_cast<double>(end) - kWindowDurationMs +
        (kWindowDurationMs * static_cast<double>(k) / (kWindowFrames - 1));
    // `target` never exceeds `end`, which is track_.back()'s own timestamp,
    // so this never advances left past track_.size() - 2: .at() still
    // throws if that ever stops holding, rather than reading past the end.
    while (static_cast<double>(track_.at(left + 1).timestamp_ms) < target) {
      ++left;
    }
    const Entry& before = track_.at(left);
    const Entry& after = track_.at(std::min(left + 1, track_.size() - 1));
    // Never 0: Add's own contract has timestamp_ms strictly increase from
    // call to call, and the while loop above never stops with `before` and
    // `after` the same entry while a later, strictly newer one remains.
    const auto span =
        static_cast<double>(after.timestamp_ms - before.timestamp_ms);
    const double fraction =
        (target - static_cast<double>(before.timestamp_ms)) / span;
    for (int c = 0; c < kRawChannels; ++c) {
      const double lo = before.frame.at(c);
      const double hi = after.frame.at(c);
      out.at(k).at(c) = static_cast<float>(lo + (fraction * (hi - lo)));
    }
  }
  return out;
}

void Window::Reset() { track_.clear(); }

}  // namespace facepalm::head_gesture
