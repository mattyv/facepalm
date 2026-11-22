#include "src/look_away_reader.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/look_away.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr std::int64_t kFrameMs = 66;  // 15 frames per second.
constexpr double kDegree = std::numbers::pi / 180;

// Where the head points, in degrees: `yaw` to the subject's left, `pitch`
// down.
struct Pose {
  double yaw = 0;
  double pitch = 0;
};

// A face whose transformation matrix is turned `pose.yaw` and then tipped
// `pose.pitch`, Ry(yaw) * Rx(pitch), the order HeadRotationFrom
// (src/head_pose.cc) reads.
Face FaceAt(Pose pose) {
  const double cy = std::cos(pose.yaw * kDegree);
  const double sy = std::sin(pose.yaw * kDegree);
  const double cp = std::cos(pose.pitch * kDegree);
  const double sp = std::sin(pose.pitch * kDegree);
  // Rows of Ry * Rx.
  const std::array<std::array<double, 3>, 3> rows = {{
      {cy, sy * sp, sy * cp},
      {0, cp, -sp},
      {-sy, cy * sp, cy * cp},
  }};
  Face face{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      face.pose_column_major[(column * 4) + row] =
          static_cast<float>(rows.at(row).at(column));
    }
  }
  face.pose_column_major[15] = 1;
  return face;
}

// Feeds a LookAwayReader frames at 15 per second and collects every
// reaction: when a look away counts is test/look_away_test.cc's job, this
// file's is the pose the reader reads from the face, the resting pose it
// takes from its ExpressionTracker, and the reaction it reports.
class LookAwayReaderTest : public ::testing::Test {
 protected:
  // `seconds` of frames, at 15 a second, with the head at `pose`.
  void Hold(double seconds, Pose pose = {}) {
    const int frames = static_cast<int>(seconds * 15);
    for (int frame = 0; frame < frames; ++frame) {
      now_ms_ += kFrameMs;
      reader_.OnFace(FaceAt(pose), /*width=*/640, /*height=*/480, now_ms_,
                     reactions_);
    }
  }

  std::int64_t now_ms_ = 1000;
  LookAwayReader reader_{
      ExpressionTracker::Settings{.rise = 0.06,
                                  .pose_degrees = 10,
                                  .pose_range = 3,
                                  .settle_ms = 600,
                                  .spreads = 3,
                                  .hold_ms = 200,
                                  .eye_look_down_threshold = 0.3,
                                  .eye_blink_threshold = 0.5},
      LookAwayDetector::Settings{.degrees = 15, .hold_ms = 1000}};
  std::vector<Reaction> reactions_;
};

TEST_F(LookAwayReaderTest, ReportsNothingForAHeadAtRest) {
  Hold(30);
  EXPECT_THAT(reactions_, IsEmpty());
}

TEST_F(LookAwayReaderTest, ReportsATurnAwayHeldForTheHoldTimeOnce) {
  Hold(5);
  Hold(3, {.yaw = 30});
  EXPECT_THAT(reactions_, ElementsAre(R"({"gesture":"looking_away"})"));
}

TEST_F(LookAwayReaderTest, ReadsPitchFromTheFaceToo) {
  Hold(5);
  Hold(3, {.pitch = 25});
  EXPECT_THAT(reactions_, ElementsAre(R"({"gesture":"looking_away"})"));
}

// Nothing is reported before the resting pose is learned: a face that
// starts out turned is the resting pose, not a look away.
TEST_F(LookAwayReaderTest, AHeadTurnedFromTheStartIsItsRestingPose) {
  Hold(10, {.yaw = 30});
  EXPECT_THAT(reactions_, IsEmpty());
}

TEST_F(LookAwayReaderTest, NodsAreNotALookAway) {
  Hold(5);
  // 20 degree nods at about one a second, for ten seconds.
  for (int nod = 0; nod < 10; ++nod) {
    Hold(0.5, {.pitch = 20});
    Hold(0.5);
  }
  EXPECT_THAT(reactions_, IsEmpty());
}

// The tracker learns afresh on its own after a gap of more than two seconds
// (ExpressionTracker::Update), and the detector holds nothing a missing
// face invalidates, so OnNoFace drops nothing.
TEST_F(LookAwayReaderTest, OnNoFaceDropsNothingOfItsOwn) {
  Hold(5);
  Hold(0.5, {.yaw = 30});
  reader_.OnNoFace();
  Hold(1, {.yaw = 30});
  EXPECT_THAT(reactions_, ElementsAre(R"({"gesture":"looking_away"})"));
}

// A face whose transformation matrix has no rotation to read (#44) is
// skipped: were its NaN pose learned, the resting pose would stay NaN, and
// every pose after it would read as away.
TEST_F(LookAwayReaderTest, ADegeneratePoseIsSkipped) {
  Hold(5);
  now_ms_ += kFrameMs;
  reader_.OnFace(Face{}, /*width=*/640, /*height=*/480, now_ms_, reactions_);
  // A NaN resting pose would read every pose as away.
  Hold(3);
  EXPECT_THAT(reactions_, IsEmpty());
  Hold(3, {.yaw = 30});
  EXPECT_THAT(reactions_, ElementsAre(R"({"gesture":"looking_away"})"));
}

}  // namespace
}  // namespace facepalm
