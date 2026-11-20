#include "src/gesture_reader.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numbers>
#include <span>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/head_gesture_model.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

using Matrix3 = std::array<std::array<double, 3>, 3>;

// Rz(roll) * Ry(yaw) * Rx(pitch), src/head_gesture_model.cc's own
// convention, so a sequence of these round-trips through RawFrameFrom back
// to the same (yaw, roll, pitch) Idiap's model reads.
Matrix3 MatrixFromYawRollPitch(double yaw, double roll, double pitch) {
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

// Undoes head_gesture::IdiapHeadPoseFromRotation's coordinate flip (D =
// diag(1,-1,-1) conjugation, which is its own inverse), so `face`'s pose
// decodes back to the (yaw, roll, pitch) given here.
Matrix3 Unflip(const Matrix3& m) {
  Matrix3 out = m;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      const bool row_flips = row == 1 || row == 2;
      const bool column_flips = column == 1 || column == 2;
      if (row_flips != column_flips) {
        out.at(row).at(column) = -out.at(row).at(column);
      }
    }
  }
  return out;
}

// Local, head-centred points for get_landmarks's five groups (left eye,
// right eye, nose, left ear, right ear), as test/head_gesture_model_test.cc
// uses to pin RawFrameFrom; here rotated to give GestureReader a face at a
// chosen (yaw, roll, pitch), in Idiap's own convention.
void SetGroup(Face& face, std::span<const int> indices,
              std::array<float, 3> point) {
  for (const int index : indices) {
    face.landmarks[index][0] = point[0];
    face.landmarks[index][1] = point[1];
    face.landmarks[index][2] = point[2];
  }
}

constexpr std::array<int, 16> kLeftEye = {130, 247, 30, 29, 27, 28, 56,  190,
                                          243, 112, 26, 22, 23, 24, 110, 25};
constexpr std::array<int, 16> kRightEye = {463, 414, 286, 258, 257, 259,
                                           260, 467, 359, 255, 339, 254,
                                           253, 252, 256, 341};
constexpr std::array<int, 15> kNose = {51,  5, 281, 45,  4,   275, 220, 134,
                                       236, 3, 195, 248, 456, 363, 440};
constexpr std::array<int, 4> kEarLeft = {234, 93, 227, 137};
constexpr std::array<int, 4> kEarRight = {454, 323, 447, 366};

// A face with its five key points and pose at (yaw, roll, pitch) radians,
// around a fixed local head rig.
Face FaceAt(double yaw, double roll, double pitch) {
  constexpr std::array<double, 3> kLeftEyeLocal = {-20, -10, 0};
  constexpr std::array<double, 3> kRightEyeLocal = {20, -10, 0};
  constexpr std::array<double, 3> kNoseLocal = {0, 5, 10};
  constexpr std::array<double, 3> kEarLeftLocal = {-40, 0, -15};
  constexpr std::array<double, 3> kEarRightLocal = {40, 0, -15};
  constexpr std::array<double, 3> kCenter = {112, 112, 40};

  const Matrix3 rotation = MatrixFromYawRollPitch(yaw, roll, pitch);
  const auto rotate = [&](const std::array<double, 3>& local) {
    std::array<float, 3> out{};
    for (int row = 0; row < 3; ++row) {
      double value = kCenter.at(row);
      for (int column = 0; column < 3; ++column) {
        value += rotation.at(row).at(column) * local.at(column);
      }
      // Points are averaged over 468-point indices, then scaled by this
      // test's 224x224 frame size (matching facepalm_mediapipe's own
      // normalised-to-image convention): divide the pixel coordinate back
      // out here so RawFrameFrom's multiply recovers it.
      out.at(static_cast<std::size_t>(row)) = static_cast<float>(value / 224.0);
    }
    return out;
  };

  Face face{};
  SetGroup(face, kLeftEye, rotate(kLeftEyeLocal));
  SetGroup(face, kRightEye, rotate(kRightEyeLocal));
  SetGroup(face, kNose, rotate(kNoseLocal));
  SetGroup(face, kEarLeft, rotate(kEarLeftLocal));
  SetGroup(face, kEarRight, rotate(kEarRightLocal));

  const Matrix3 unflipped = Unflip(MatrixFromYawRollPitch(yaw, roll, pitch));
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) {
      face.pose_column_major[(column * 4) + row] =
          static_cast<float>(unflipped.at(row).at(column));
    }
  }
  face.pose_column_major[15] = 1;
  return face;
}

// Feeds `reader` a 50-frame, 15 fps track at the given (yaw, roll, pitch)
// function of time, and returns the reactions from the last frame only
// (every earlier one is "not ready yet", per head_gesture::Window).
std::vector<Reaction> FeedTrack(
    GestureReader& reader,
    const std::function<std::array<double, 3>(double)>& angles_at,
    std::int64_t start_ms = 0) {
  constexpr int kFrames = 50;
  constexpr double kFrameSeconds = 1.0 / 15.0;
  std::vector<Reaction> reactions;
  for (int i = 0; i < kFrames; ++i) {
    const double t = i * kFrameSeconds;
    const std::array<double, 3> angles = angles_at(t);
    reactions.clear();
    reader.OnFace(FaceAt(angles[0], angles[1], angles[2]), 224, 224,
                  start_ms + static_cast<std::int64_t>(i * 1000.0 / 15.0),
                  reactions);
  }
  return reactions;
}

constexpr double kDegree = std::numbers::pi / 180.0;
constexpr std::int64_t kFrameGapMs = 67;

// A 5-degree roll oscillation at 1.5 Hz reads as a tilt with probability
// 1.0 (found by sweeping the real model over synthetic rigs; #34).
std::array<double, 3> Tilt(double t) {
  constexpr double kAmplitude = 5 * kDegree;
  constexpr double kFrequencyHz = 1.5;
  return {0.0, kAmplitude * std::sin(2 * std::numbers::pi * kFrequencyHz * t),
          0.0};
}

TEST(GestureReaderTest, MakesNoReactionUntilTheWindowIsReady) {
  GestureReader reader(0.5F);
  std::vector<Reaction> reactions;
  for (int i = 0; i < head_gesture::kMinTrackFrames - 1; ++i) {
    reader.OnFace(FaceAt(0, 0, 0), 224, 224,
                  static_cast<std::int64_t>(i) * kFrameGapMs, reactions);
  }
  EXPECT_THAT(reactions, IsEmpty());
}

TEST(GestureReaderTest, ReactsToATiltOnceItStarts) {
  GestureReader reader(0.5F);
  const std::vector<Reaction> first = FeedTrack(reader, Tilt);
  EXPECT_THAT(first, ElementsAre(R"({"gesture":"tilt"})"));

  // Still tilting: already reported, so no second reaction.
  std::vector<Reaction> again;
  reader.OnFace(FaceAt(0, 5 * kDegree, 0), 224, 224,
                (head_gesture::kMinTrackFrames * kFrameGapMs) + kFrameGapMs,
                again);
  EXPECT_THAT(again, IsEmpty());
}

TEST(GestureReaderTest, OnNoFaceDropsTheWindowAndTheReportedGesture) {
  GestureReader reader(0.5F);
  ASSERT_THAT(FeedTrack(reader, Tilt), ElementsAre(R"({"gesture":"tilt"})"));

  reader.OnNoFace();

  // The window is gone, so the next 49 frames are "not ready yet" again,
  // and the 50th reports afresh rather than staying silent.
  const std::vector<Reaction> after = FeedTrack(reader, Tilt, 1'000'000);
  EXPECT_THAT(after, ElementsAre(R"({"gesture":"tilt"})"));
}

TEST(GestureReaderTest, AStationaryFaceNeverReacts) {
  GestureReader reader(0.5F);
  const auto still = [](double /*t*/) -> std::array<double, 3> {
    return {0.0, 0.0, 0.0};
  };
  EXPECT_THAT(FeedTrack(reader, still), IsEmpty());
}

}  // namespace
}  // namespace facepalm
