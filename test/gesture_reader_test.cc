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

// A 15 fps track long enough that, once the window is ready at
// head_gesture::kMinTrackFrames, a class the model holds continuously still
// has time to clear GestureReportGate's confirm_ms before the track ends
// (#34): kFeedFrames - kMinTrackFrames frames at ~66.67ms apart is about
// 667ms, well over confirm_ms (200ms, gesture_reader.cc's kConfirmMs).
constexpr int kFeedFrames = 60;

// The timestamp FeedTrack gives frame `i` of a track starting at `start_ms`.
std::int64_t FeedTimestampMs(std::int64_t start_ms, int i) {
  return start_ms + static_cast<std::int64_t>(i * 1000.0 / 15.0);
}

// Feeds `reader` a kFeedFrames-frame, 15 fps track at the given (yaw, roll,
// pitch) function of time, and returns every reaction from the whole track,
// in order (the frames before head_gesture::kMinTrackFrames are all "not
// ready yet", per head_gesture::Window, and so never add one).
std::vector<Reaction> FeedTrack(
    GestureReader& reader,
    const std::function<std::array<double, 3>(double)>& angles_at,
    std::int64_t start_ms = 0) {
  constexpr double kFrameSeconds = 1.0 / 15.0;
  std::vector<Reaction> all;
  std::vector<Reaction> reactions;
  for (int i = 0; i < kFeedFrames; ++i) {
    const double t = i * kFrameSeconds;
    const std::array<double, 3> angles = angles_at(t);
    reactions.clear();
    reader.OnFace(FaceAt(angles[0], angles[1], angles[2]), 224, 224,
                  FeedTimestampMs(start_ms, i), reactions);
    all.insert(all.end(), reactions.begin(), reactions.end());
  }
  return all;
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
  const std::vector<Reaction> reactions = FeedTrack(reader, Tilt);
  EXPECT_THAT(reactions, ElementsAre(R"({"gesture":"tilt"})"));

  // Still tilting: the episode from the one report above is still open, so
  // no second reaction.
  std::vector<Reaction> again;
  reader.OnFace(FaceAt(0, 5 * kDegree, 0), 224, 224,
                FeedTimestampMs(0, kFeedFrames), again);
  EXPECT_THAT(again, IsEmpty());
}

TEST(GestureReaderTest, OnNoFaceDropsTheWindowAndTheReportedGesture) {
  GestureReader reader(0.5F);
  ASSERT_THAT(FeedTrack(reader, Tilt), ElementsAre(R"({"gesture":"tilt"})"));

  reader.OnNoFace();

  // The window and the gate are both gone, so the next kMinTrackFrames - 1
  // frames are "not ready yet" again, and the track reports tilt afresh
  // rather than staying silent (the old episode cannot still be open).
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

// GestureReportGate, directly, on chosen class sequences: the episode rule
// (#34) separated from the model so it can be pinned without needing the
// real model to produce a particular run of classes. gesture_reader.cc's
// kConfirmMs (200) and kStillMs (750) are repeated here as literals, since
// the rule being tested is specifically that those durations are honoured.
using head_gesture::Gesture;

TEST(GestureReportGateTest,
     AlternatingClassesInsideOneMovementGiveExactlyOneReport) {
  GestureReportGate gate;
  std::vector<Gesture> reported;
  const auto feed = [&](Gesture gesture, std::int64_t timestamp_ms) {
    const Gesture result = gate.Update(gesture, timestamp_ms);
    if (result != Gesture::kNone) {
      reported.push_back(result);
    }
  };

  // Confirms shake over 200ms, opening an episode.
  feed(Gesture::kShake, 0);
  feed(Gesture::kShake, 200);
  // Flips between shake and turn seven times, as the live recording did,
  // 60-730ms apart: the episode stays open, so none of these report.
  feed(Gesture::kTurn, 260);
  feed(Gesture::kShake, 350);
  feed(Gesture::kTurn, 450);
  feed(Gesture::kShake, 700);
  feed(Gesture::kTurn, 900);
  feed(Gesture::kShake, 1000);
  feed(Gesture::kTurn, 1070);

  EXPECT_THAT(reported, ElementsAre(Gesture::kShake));
}

TEST(GestureReportGateTest,
     ASingleNoneFrameInsideAGestureDoesNotEndTheEpisode) {
  GestureReportGate gate;
  ASSERT_EQ(gate.Update(Gesture::kNod, 0), Gesture::kNone);
  ASSERT_EQ(gate.Update(Gesture::kNod, 200), Gesture::kNod);  // Reported.

  // One stray kNone frame, far short of still_ms.
  EXPECT_EQ(gate.Update(Gesture::kNone, 210), Gesture::kNone);

  // The episode is still open, so nod does not get reported a second time
  // even once it would otherwise have held long enough to confirm again;
  // if the kNone frame above had wrongly ended the episode, this would
  // report.
  EXPECT_EQ(gate.Update(Gesture::kNod, 220), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kNod, 420), Gesture::kNone);
}

TEST(GestureReportGateTest, AStillHeadShortOfStillMsDoesNotEndTheEpisode) {
  GestureReportGate gate;
  ASSERT_EQ(gate.Update(Gesture::kNod, 0), Gesture::kNone);
  ASSERT_EQ(gate.Update(Gesture::kNod, 200), Gesture::kNod);  // Reported.

  // One ms short of still_ms at the last kNone frame (749ms from the
  // first), and the head moves again at the 750ms mark itself: only a
  // kNone frame can close the episode, so it stays open, and even a
  // gesture that would otherwise confirm right after is not reported.
  EXPECT_EQ(gate.Update(Gesture::kNone, 300), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kNone, 1049), Gesture::kNone);  // 749ms.
  EXPECT_EQ(gate.Update(Gesture::kShake, 1050), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kShake, 1250), Gesture::kNone);  // 200ms.
}

TEST(GestureReportGateTest,
     AStillHeadForStillMsEndsTheEpisodeAndTheNextGestureIsReported) {
  GestureReportGate gate;
  ASSERT_EQ(gate.Update(Gesture::kNod, 0), Gesture::kNone);
  ASSERT_EQ(gate.Update(Gesture::kNod, 200), Gesture::kNod);  // Reported.

  // still_ms (750ms, from the first kNone) closes the episode.
  EXPECT_EQ(gate.Update(Gesture::kNone, 300), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kNone, 1050), Gesture::kNone);  // 750ms.

  // The episode is closed, so a new gesture confirms and reports again.
  EXPECT_EQ(gate.Update(Gesture::kShake, 1100), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kShake, 1300), Gesture::kShake);  // 200ms.
}

TEST(GestureReportGateTest, AClassThatDoesNotHoldForConfirmMsIsNotReported) {
  GestureReportGate gate;
  // One frame of nod among none: never holds long enough to confirm.
  EXPECT_EQ(gate.Update(Gesture::kNone, 0), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kNod, 100), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kNone, 150), Gesture::kNone);
}

TEST(GestureReportGateTest,
     ACandidateThatChangesClassBeforeConfirmMsStartsAgainForTheNewClass) {
  GestureReportGate gate;
  EXPECT_EQ(gate.Update(Gesture::kNod, 0), Gesture::kNone);
  // Changes before nod's 200ms is up: the count restarts for shake.
  EXPECT_EQ(gate.Update(Gesture::kShake, 50), Gesture::kNone);
  // 250 - 50 = 200ms of shake: reports shake, not nod.
  EXPECT_EQ(gate.Update(Gesture::kShake, 250), Gesture::kShake);
}

TEST(GestureReportGateTest, TurnWaitsForReturnAndStillnessBeforeRearming) {
  GestureReportGate gate;
  using head_gesture::Gesture;
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 1000, {.yaw = 0.3, .window_start_yaw = 0.0}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 1200, {.yaw = 0.4, .window_start_yaw = 0.0}),
      Gesture::kTurn);
  EXPECT_EQ(
      gate.Update(Gesture::kNone, 2000, {.yaw = 0.4, .window_start_yaw = 0.4}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kNone, 3000, {.yaw = 0.4, .window_start_yaw = 0.4}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 3100, {.yaw = 0.4, .window_start_yaw = 0.4}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kNone, 4000, {.yaw = 0.05, .window_start_yaw = 0.4}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 4300, {.yaw = 0.05, .window_start_yaw = 0.4}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kNone, 4400, {.yaw = 0.05, .window_start_yaw = 0.4}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kNone, 5200, {.yaw = 0.05, .window_start_yaw = 0.4}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 5300, {.yaw = 0.3, .window_start_yaw = 0.05}),
      Gesture::kNone);
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 5500, {.yaw = 0.4, .window_start_yaw = 0.05}),
      Gesture::kTurn);
}

TEST(GestureReportGateTest,
     TurnLatchDoesNotSuppressOtherGesturesAndResetClearsIt) {
  GestureReportGate gate;
  using head_gesture::Gesture;
  gate.Update(Gesture::kTurn, 0, {.yaw = 0.3, .window_start_yaw = 0.0});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 200, {.yaw = 0.4, .window_start_yaw = 0.0}),
      Gesture::kTurn);
  gate.Update(Gesture::kNone, 300, {.yaw = 0.4, .window_start_yaw = 0.4});
  gate.Update(Gesture::kNone, 1100, {.yaw = 0.4, .window_start_yaw = 0.4});
  gate.Update(Gesture::kNod, 1200, {.yaw = 0.4, .window_start_yaw = 0.4});
  EXPECT_EQ(
      gate.Update(Gesture::kNod, 1400, {.yaw = 0.4, .window_start_yaw = 0.4}),
      Gesture::kNod);
  gate.Reset();
  gate.Update(Gesture::kTurn, 1500, {.yaw = 0.4, .window_start_yaw = 0.4});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 1700, {.yaw = 0.5, .window_start_yaw = 0.4}),
      Gesture::kTurn);
}

TEST(GestureReportGateTest, SuppressedTurnsDoNotMakeTheGeneralGateStill) {
  GestureReportGate gate;
  using head_gesture::Gesture;
  gate.Update(Gesture::kTurn, 0, {.yaw = 0.3, .window_start_yaw = 0.0});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 200, {.yaw = 0.4, .window_start_yaw = 0.0}),
      Gesture::kTurn);
  gate.Update(Gesture::kTurn, 300, {.yaw = 0.4, .window_start_yaw = 0.0});
  gate.Update(Gesture::kTurn, 1100, {.yaw = 0.4, .window_start_yaw = 0.0});
  gate.Update(Gesture::kNod, 1200, {.yaw = 0.4, .window_start_yaw = 0.0});
  EXPECT_EQ(
      gate.Update(Gesture::kNod, 1400, {.yaw = 0.4, .window_start_yaw = 0.0}),
      Gesture::kNone);
}

TEST(GestureReportGateTest, ReturnMustRemainNearForTheFullStillTime) {
  GestureReportGate gate;
  using head_gesture::Gesture;
  gate.Update(Gesture::kTurn, 0, {.yaw = 0.3, .window_start_yaw = 0.0});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 200, {.yaw = 0.4, .window_start_yaw = 0.0}),
      Gesture::kTurn);
  gate.Update(Gesture::kNone, 300, {.yaw = 0.0, .window_start_yaw = 0.0});
  gate.Update(Gesture::kNone, 1049, {.yaw = 0.0, .window_start_yaw = 0.0});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 1050, {.yaw = 0.3, .window_start_yaw = 0.0}),
      Gesture::kNone);
  gate.Update(Gesture::kNone, 1100, {.yaw = 0.0, .window_start_yaw = 0.0});
  gate.Update(Gesture::kNone, 1500, {.yaw = 0.3, .window_start_yaw = 0.0});
  gate.Update(Gesture::kNone, 1600, {.yaw = 0.0, .window_start_yaw = 0.0});
  gate.Update(Gesture::kNone, 1900, {.yaw = 0.0, .window_start_yaw = 0.0});
  gate.Update(Gesture::kTurn, 2000, {.yaw = 0.3, .window_start_yaw = 0.0});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 2200, {.yaw = 0.4, .window_start_yaw = 0.0}),
      Gesture::kNone);
  gate.Update(Gesture::kNone, 2300, {.yaw = 0.0, .window_start_yaw = 0.0});
  gate.Update(Gesture::kNone, 3050, {.yaw = 0.0, .window_start_yaw = 0.0});
  gate.Update(Gesture::kTurn, 3100, {.yaw = 0.3, .window_start_yaw = 0.0});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 3300, {.yaw = 0.4, .window_start_yaw = 0.0}),
      Gesture::kTurn);
}

TEST(GestureReportGateTest, ReturnDistanceWrapsAroundPi) {
  GestureReportGate gate;
  using head_gesture::Gesture;
  gate.Update(Gesture::kTurn, 0, {.yaw = 2.8, .window_start_yaw = 3.13});
  EXPECT_EQ(
      gate.Update(Gesture::kTurn, 200, {.yaw = 2.8, .window_start_yaw = 3.13}),
      Gesture::kTurn);
  gate.Update(Gesture::kNone, 300, {.yaw = -3.13, .window_start_yaw = -3.13});
  gate.Update(Gesture::kNone, 1100, {.yaw = -3.13, .window_start_yaw = -3.13});
  gate.Update(Gesture::kTurn, 1200, {.yaw = 2.8, .window_start_yaw = -3.13});
  EXPECT_EQ(gate.Update(Gesture::kTurn, 1400,
                        {.yaw = 2.8, .window_start_yaw = -3.13}),
            Gesture::kTurn);
}

TEST(GestureReportGateTest, ResetClearsAPendingCandidate) {
  GestureReportGate gate;
  ASSERT_EQ(gate.Update(Gesture::kNod, 0), Gesture::kNone);

  gate.Reset();

  // Had the candidate survived, 300 - 0 = 300ms would have confirmed it
  // immediately; instead this starts a fresh candidate.
  EXPECT_EQ(gate.Update(Gesture::kNod, 300), Gesture::kNone);
}

TEST(GestureReportGateTest, ResetClosesAnOpenEpisode) {
  GestureReportGate gate;
  ASSERT_EQ(gate.Update(Gesture::kNod, 0), Gesture::kNone);
  ASSERT_EQ(gate.Update(Gesture::kNod, 200), Gesture::kNod);  // Reported.

  gate.Reset();

  // Had the episode stayed open, nothing here would ever report again; with
  // no still_ms of kNone, that is only possible once Reset has closed it.
  EXPECT_EQ(gate.Update(Gesture::kNod, 1000), Gesture::kNone);
  EXPECT_EQ(gate.Update(Gesture::kNod, 1200), Gesture::kNod);  // 200ms.
}

TEST(GestureReportGateTest, TheRuleHoldsByElapsedTimeNotByFrameCount) {
  // 1 frame per second: confirms in a single extra call.
  GestureReportGate one_fps;
  EXPECT_EQ(one_fps.Update(Gesture::kNod, 0), Gesture::kNone);
  EXPECT_EQ(one_fps.Update(Gesture::kNod, 1000), Gesture::kNod);

  // 60 frames per second (~16.667ms apart): takes many more calls to reach
  // the same 200ms, proving the gate counts milliseconds, not calls.
  GestureReportGate sixty_fps;
  constexpr double kFrameMs = 1000.0 / 60.0;
  Gesture last = Gesture::kNone;
  int calls = 0;
  for (int i = 0; last == Gesture::kNone && i < 100; ++i) {
    last = sixty_fps.Update(Gesture::kNod,
                            static_cast<std::int64_t>(i * kFrameMs));
    ++calls;
  }
  EXPECT_EQ(last, Gesture::kNod);
  EXPECT_GT(calls, 10);  // Far more than the 2 calls 1 fps needed.
}

}  // namespace
}  // namespace facepalm
