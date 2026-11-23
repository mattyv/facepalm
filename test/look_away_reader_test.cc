#include "src/look_away_reader.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

#include "src/calibration.h"
#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/look_away.h"

namespace facepalm {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;

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
// (src/head_pose.cc) reads, with `eye_look_out` set on src/eye_gaze.cc's
// own eyeLookOutLeft (15) and eyeLookInRight (14) blendshapes alike, so
// EyeSignalFrom's horizontal signal (#170) reads exactly `eye_look_out`:
// (eye_look_out - 0 + eye_look_out - 0) / 2.
Face FaceAt(Pose pose, double eye_look_out = 0) {
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
  face.blendshapes[14] = static_cast<float>(eye_look_out);
  face.blendshapes[15] = static_cast<float>(eye_look_out);
  return face;
}

// A reaction naming `kind`, whatever its own exact duration_ms (frame
// arithmetic in Hold's own 66ms steps is exact but not worth spelling
// out): test/look_away_test.cc is where duration is covered exactly.
::testing::Matcher<const Reaction&> LookedAway(std::string_view kind) {
  return AllOf(HasSubstr(R"("gesture":"looked_away")"),
               HasSubstr(R"("kind":")" + std::string(kind) + R"(")"));
}

// Feeds a LookAwayReader frames at 15 per second and collects every
// reaction: when a look away counts, and its kind, is
// test/look_away_test.cc's job; this file's is the gaze the reader
// composes from the face, the resting pose it takes from its
// ExpressionTracker, and the reaction it reports.
class LookAwayReaderTest : public ::testing::Test {
 protected:
  // `seconds` of frames, at 15 a second, with the head at `pose` and,
  // for #170's own gains test, the eyes looking out by `eye_look_out`.
  void Hold(double seconds, Pose pose = {}, double eye_look_out = 0) {
    const int frames = static_cast<int>(seconds * 15);
    for (int frame = 0; frame < frames; ++frame) {
      now_ms_ += kFrameMs;
      reader_.OnFace(FaceAt(pose, eye_look_out), /*width=*/640,
                     /*height=*/480, now_ms_, reactions_);
    }
  }

  static LookAwayDetector::Settings DetectorSettings() {
    return {.degrees = 15,
            .hold_ms = 1000,
            .averted_ms = 6000,
            .other_screen_margin_degrees = 3};
  }

  static ExpressionTracker::Settings TrackerSettings() {
    return {.rise = 0.06,
            .pose_degrees = 10,
            .pose_range = 3,
            .settle_ms = 600,
            .spreads = 3,
            .hold_ms = 200,
            .eye_look_down_threshold = 0.3,
            .eye_blink_threshold = 0.5};
  }

  static EyeGazeTracker::Settings EyeTrackerSettings() {
    return {.blink_threshold = 0.5,
            .median_ms = 0,
            .resting_ms = 0,
            .gap_ms = 2000};
  }

  std::int64_t now_ms_ = 1000;
  LookAwayReader reader_{TrackerSettings(), EyeTrackerSettings(),
                         DetectorSettings()};
  std::vector<Reaction> reactions_;
};

TEST_F(LookAwayReaderTest, ReportsNothingForAHeadAtRest) {
  Hold(30);
  EXPECT_THAT(reactions_, IsEmpty());
}

TEST_F(LookAwayReaderTest, ReportsATurnAwayOnceItReturns) {
  Hold(5);
  Hold(3, {.yaw = 30});
  EXPECT_THAT(reactions_, IsEmpty());  // Not yet: still away.
  Hold(1);
  EXPECT_THAT(reactions_, ElementsAre(LookedAway("averted")));
}

TEST_F(LookAwayReaderTest, ReadsPitchFromTheFaceToo) {
  Hold(5);
  Hold(3, {.pitch = 25});
  Hold(1);
  EXPECT_THAT(reactions_, ElementsAre(LookedAway("averted")));
}

// Nothing is reported before the resting pose is learned: a face that
// starts out turned is the resting pose, not a look away.
TEST_F(LookAwayReaderTest, AHeadTurnedFromTheStartIsItsRestingPose) {
  Hold(10, {.yaw = 30});
  Hold(1);
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
  Hold(1);
  EXPECT_THAT(reactions_, ElementsAre(LookedAway("averted")));
}

// #141's kGone: OnNoFace, not OnFace, says there was no face. The look
// reported on return spans the whole stretch, read in real time from the
// last timestamp OnFace gave (OnNoFace has none of its own).
TEST_F(LookAwayReaderTest, OnNoFaceReportsGoneOnceAFaceReturns) {
  Hold(5);
  reader_.OnNoFace();
  reader_.OnNoFace();
  // The gap itself, in real time: OnNoFace is given none of its own, so
  // the reader measures it from the last timestamp OnFace gave.
  now_ms_ += 1500;
  Hold(1);  // A face returns, at rest: the gap itself is the whole look.
  EXPECT_THAT(reactions_, ElementsAre(LookedAway("gone")));
}

// Past ExpressionTracker's own two-second gap (src/expressions.cc), the
// face that returns relearns its resting pose afresh -- but only from the
// frame after this one (#141): this one must still read the resting pose
// from before the gap, or the look the gap itself opened would never be
// reported at all.
TEST_F(LookAwayReaderTest, OnNoFacePastTheTrackersOwnGapStillReportsGone) {
  Hold(5);
  reader_.OnNoFace();
  now_ms_ += 3000;
  Hold(1);
  EXPECT_THAT(reactions_, ElementsAre(LookedAway("gone")));
}

// facepalm#157: a face reacquired too far off the old resting pose for
// #141's own one grace frame (read against the resting pose from before
// the gap) to close the look there and then must still be reported, not
// dropped the moment ExpressionTracker starts relearning the resting
// pose afresh -- `test/fixtures/eyes/fully_away.probe` (#139) is a real
// recording of exactly this shape.
TEST_F(LookAwayReaderTest,
       ALookThatOutlastsTheTrackersOwnRelearningStillReportsGone) {
  Hold(5);
  reader_.OnNoFace();
  now_ms_ += 4000;  // Past the tracker's own two-second relearning gap.
  // The face returns turned well away: too far for the one grace frame
  // to read as home.
  Hold(0.1, {.yaw = 30});
  EXPECT_THAT(reactions_, IsEmpty());  // Not yet: still relearning.
  // Held near the true resting pose for the rest of the relearning
  // window: by the time it finishes, the newly learned resting pose has
  // caught back up to roughly where it always was, not to this turn.
  Hold(4);
  EXPECT_THAT(reactions_, ElementsAre(LookedAway("gone")));
}

// A no-face stretch too short to hold is a glance, as any other is.
TEST_F(LookAwayReaderTest, ABriefOnNoFaceIsAGlance) {
  Hold(5);
  reader_.OnNoFace();
  Hold(1);
  EXPECT_THAT(reactions_, IsEmpty());
}

// OnNoFace before any OnFace at all has no real timestamp to open or
// continue a look from (#157): last_timestamp_ms_ is still only its own
// default, not a reading OnFace ever took, so this is not yet a look to
// hold, not kGone. LookAwayDetector itself would still open one from
// such a call (GoneKindTest.NoFaceFromTheVeryFirstFrameStartsTheLookToo,
// at the detector level, where 0 is a deliberate, meaningful test
// timestamp); the reader holds back only because it has nothing
// meaningful of its own to give it yet.
TEST_F(LookAwayReaderTest, OnNoFaceBeforeAnyFaceIsNotYetALook) {
  reader_.OnNoFace();
  Hold(10);
  EXPECT_THAT(reactions_, IsEmpty());
}

// #141's kOtherScreen: SetProfile wires a calibration profile's own box
// test into the detector this reader owns.
TEST_F(LookAwayReaderTest, SetProfileWiresInTheOtherScreenBoxTest) {
  ScreenCalibration screen;
  screen.points[static_cast<std::size_t>(CalibrationPoint::kTopLeft)] = {
      .yaw_degrees = 20, .pitch_degrees = -5};
  screen.points[static_cast<std::size_t>(CalibrationPoint::kTopRight)] = {
      .yaw_degrees = 40, .pitch_degrees = -5};
  screen.points[static_cast<std::size_t>(CalibrationPoint::kBottomLeft)] = {
      .yaw_degrees = 20, .pitch_degrees = 5};
  screen.points[static_cast<std::size_t>(CalibrationPoint::kBottomRight)] = {
      .yaw_degrees = 40, .pitch_degrees = 5};
  CalibrationProfile profile;
  profile.screens = {screen};
  reader_.SetProfile(std::make_shared<const CalibrationProfile>(profile));

  Hold(5);
  Hold(3, {.yaw = 30});  // Inside the profile's own box.
  Hold(1);

  EXPECT_THAT(reactions_, ElementsAre(LookedAway("other_screen")));
}

TEST_F(LookAwayReaderTest, SetProfileOfNullptrGoesBackToNoBoxTest) {
  reader_.SetProfile(std::make_shared<const CalibrationProfile>());
  reader_.SetProfile(nullptr);

  Hold(5);
  Hold(3, {.yaw = 30});
  Hold(1);

  EXPECT_THAT(reactions_, ElementsAre(LookedAway("averted")));
}

TEST_F(LookAwayReaderTest, TwoLooksAreTwoReports) {
  Hold(5);
  Hold(3, {.yaw = 30});
  Hold(1);
  Hold(3, {.yaw = 30});
  Hold(1);
  EXPECT_THAT(reactions_, SizeIs(2));
}

// #170: reader_ is built with EyeGains{} (0, 0) -- with no gain of its
// own, the eyes alone, head held at rest, never move gaze far enough to
// read as away. Once a profile with its own gains is set, OnFace must
// compose gaze with the profile's own gains, not reader_'s ctor value,
// the way ScreenContextReporter::ZoneOf already does: the very same eyes
// then read as a turn well past DetectorSettings' 15 degrees.
TEST_F(LookAwayReaderTest, SetProfileSwapsToTheProfilesOwnGains) {
  Hold(5);
  Hold(3, {}, /*eye_look_out=*/1);
  Hold(1);
  EXPECT_THAT(reactions_, IsEmpty());

  CalibrationProfile profile;
  profile.gains = {.horizontal = 20};
  reader_.SetProfile(std::make_shared<const CalibrationProfile>(profile));

  Hold(3, {}, /*eye_look_out=*/1);
  Hold(1);
  EXPECT_THAT(reactions_, ElementsAre(LookedAway("averted")));
}

// facepalm#167: a second calibration used to copy-assign the profile
// this reader already held a raw, atomic pointer to, in place, while
// OnFace read it on another thread with no lock the writer respected.
// SetProfile now takes shared ownership of a wholly fresh
// CalibrationProfile each call, and OnFace/OnNoFace take their own
// snapshot under profile_mutex_ (Profile(), src/look_away_reader.cc), so
// profile_'s pointee is never mutated, only swapped as a whole: this is
// this reader's own regression test for that, caught by ThreadSanitizer
// (CI's thread-sanitizer job) before the fix.
TEST_F(LookAwayReaderTest, SetProfileRunsSafelyAlongsideOnFaceOnAnotherThread) {
  std::atomic<bool> stop{false};
  std::thread frames([this, &stop] {
    std::int64_t frame_ms = 1000;
    while (!stop.load()) {
      frame_ms += kFrameMs;
      std::vector<Reaction> reactions;
      reader_.OnFace(FaceAt({.yaw = 10}), /*width=*/640, /*height=*/480,
                     frame_ms, reactions);
    }
  });

  // Recalibrates with a different number of screens each time: a fresh
  // CalibrationProfile, never one the frames thread may already hold its
  // own reference to.
  for (int screens = 1; screens <= 4; ++screens) {
    CalibrationProfile profile;
    profile.screens.assign(static_cast<std::size_t>(screens),
                           ScreenCalibration{});
    reader_.SetProfile(std::make_shared<const CalibrationProfile>(profile));
  }

  stop = true;
  frames.join();
}

}  // namespace
}  // namespace facepalm
