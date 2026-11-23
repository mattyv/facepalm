#include "src/screen_context_reporter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "src/calibration.h"
#include "src/eye_gaze.h"
#include "src/eye_signs.h"
#include "src/head_pose.h"
#include "src/screen_context.h"
#include "src/screen_source.h"

namespace facepalm {
namespace {

using ::testing::Return;

constexpr GazeSettings kSettings = {.other_screen_degrees = 7.5,
                                    .away_degrees = 15};
constexpr HeadRotation kRest = {};

class MockScreenSource : public ScreenSource {
 public:
  MOCK_METHOD(std::optional<ActiveApp>, CurrentApp, (bool), (override));
  MOCK_METHOD(std::optional<MousePosition>, Mouse, (), (override));
  MOCK_METHOD(std::vector<ScreenRect>, Screens, (), (override));
};

TEST(ScreenContextReporterTest, EverythingKnownWithTitlesOff) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false))
      .WillOnce(Return(ActiveApp{.name = "Code", .title = "ignored.cc"}));
  EXPECT_CALL(source, Mouse())
      .WillOnce(Return(MousePosition{.x = 12, .y = 34}));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);
  EXPECT_EQ(
      reporter.Json(kRest, kRest),
      R"({"active_app":"Code","mouse":{"x":12,"y":34},"gaze":"this_screen"})");
}

TEST(ScreenContextReporterTest, AskedWithTitlesOnAndOneIsGiven) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(true))
      .WillOnce(Return(ActiveApp{.name = "Code", .title = "app.js"}));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/true);
  EXPECT_EQ(
      reporter.Json(kRest, kRest),
      R"({"active_app":"Code","window_title":"app.js","gaze":"this_screen"})");
}

TEST(ScreenContextReporterTest, TitlesOnWithNoneAvailableLeavesItOut) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(true))
      .WillOnce(Return(ActiveApp{.name = "Code", .title = std::nullopt}));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/true);
  EXPECT_EQ(reporter.Json(kRest, kRest),
            R"({"active_app":"Code","gaze":"this_screen"})");
}

TEST(ScreenContextReporterTest, NoAppOrMouseLeavesThemOut) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);
  EXPECT_EQ(reporter.Json(kRest, kRest), R"({"gaze":"this_screen"})");
}

TEST(ScreenContextReporterTest, NoPoseIsUnknownGaze) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);
  EXPECT_EQ(reporter.Json(std::nullopt, kRest), R"({"gaze":"unknown"})");
}

TEST(ScreenContextReporterTest, ScreensShapeTheGaze) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens())
      .WillOnce(Return(std::vector<ScreenRect>{
          {.x = 0, .y = 0, .width = 1000, .height = 800, .main = true},
          {.x = -1000, .y = 0, .width = 1000, .height = 800}}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);
  // Positive yaw (a turn to the subject's left) with a screen to the left.
  const std::optional<HeadRotation> turned = HeadRotation{.yaw_degrees = 10};
  EXPECT_EQ(reporter.Json(turned, kRest), R"({"gaze":"other_screen"})");
}

TEST(ScreenContextReporterTest, NamesEscapeAsJsonText) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false))
      .WillOnce(Return(ActiveApp{.name = R"(Quo"te)", .title = std::nullopt}));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);
  EXPECT_EQ(reporter.Json(kRest, kRest),
            R"({"active_app":"Quo\"te","gaze":"this_screen"})");
}

// A calibration profile (#61 item 12) is optional, and additive: it never
// changes CoarseGazeFrom's own reading, only adds a more precise one
// alongside it.
CalibrationProfile TwoScreenProfile() {
  CalibrationProfile profile;
  ScreenCalibration left;
  left.points[static_cast<std::size_t>(CalibrationPoint::kCentre)] =
      HeadRotation{.yaw_degrees = -30};
  ScreenCalibration right;
  right.points[static_cast<std::size_t>(CalibrationPoint::kCentre)] =
      HeadRotation{.yaw_degrees = 30};
  profile.screens = {left, right};
  return profile;
}

TEST(ScreenContextReporterTest, ACalibrationProfileAddsTheNearestScreen) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const CalibrationProfile profile = TwoScreenProfile();
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false, &profile);
  const std::optional<HeadRotation> pose = HeadRotation{.yaw_degrees = 25};

  EXPECT_EQ(reporter.Json(pose, kRest),
            R"({"gaze":"away","calibrated_screen":1})");
}

TEST(ScreenContextReporterTest, NoPoseLeavesTheCalibratedScreenOut) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const CalibrationProfile profile = TwoScreenProfile();
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false, &profile);

  EXPECT_EQ(reporter.Json(std::nullopt, kRest), R"({"gaze":"unknown"})");
}

TEST(ScreenContextReporterTest, AnEmptyProfileLeavesTheCalibratedScreenOut) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const CalibrationProfile empty;
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false, &empty);

  EXPECT_EQ(reporter.Json(kRest, kRest), R"({"gaze":"this_screen"})");
}

TEST(ScreenContextReporterTest, NoProfileGivenLeavesTheCalibratedScreenOut) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);

  EXPECT_EQ(reporter.Json(kRest, kRest), R"({"gaze":"this_screen"})");
}

// #137's own composition: an eye signal composed with head pose can turn
// an uncalibrated this_screen reading away, where the raw head pose alone
// would not have.
TEST(ScreenContextReporterTest, EyeSignalComposesIntoTheGazeReading) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false, nullptr,
                                       EyeGains{.horizontal = 30});
  const EyeSignal eye{.horizontal = 1};
  const EyeSignal eye_resting{};

  EXPECT_EQ(reporter.Json(kRest, kRest, eye, eye_resting),
            R"({"gaze":"away"})");
}

// A profile's own gains take over from the constructor's default once one
// is given (#137): the same eye signal that composed into kThisScreen with
// no profile's gains of 0 composes into kAway with the profile's own,
// much larger, gains.
TEST(ScreenContextReporterTest, AProfilesOwnGainsTakeOverFromTheDefault) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false)).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).WillOnce(Return(std::nullopt));
  EXPECT_CALL(source, Screens()).WillOnce(Return(std::vector<ScreenRect>{}));
  CalibrationProfile profile;
  profile.gains = {.horizontal = 30};
  profile.screens = {ScreenCalibration{}};  // One screen, centred at (0, 0).
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false, &profile);
  const EyeSignal eye{.horizontal = 1};
  const EyeSignal eye_resting{};

  EXPECT_EQ(reporter.Json(kRest, kRest, eye, eye_resting),
            R"({"gaze":"away","calibrated_screen":0})");
}

// The Sample/Between tests below stand for #142's and #144's own beat
// context: the majority gaze over a window, and whether it was mostly
// inside a content rectangle.

constexpr ScreenRect kMain = {
    .x = 0, .y = 0, .width = 1000, .height = 800, .main = true};
constexpr ContentRect kCentre = {
    .x = 400, .y = 300, .width = 200, .height = 200};
constexpr ContentRect kTopLeftCorner = {
    .x = 0, .y = 0, .width = 50, .height = 50};

TEST(BetweenTest, NoSamplesInRangeIsUnknown) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);

  const GazeWindow window = reporter.Between(0, 1000, kCentre);

  EXPECT_EQ(window.gaze, Gaze::kUnknown);
  EXPECT_EQ(window.content, ContentHit::kUnknown);
}

// With no screen marked main, Between falls back to an empty ScreenRect
// for ContentHitFrom's own `main` rather than naming one arbitrarily.
TEST(BetweenTest, NoMainScreenStillAnswersTheMajorityZone) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{
          {.x = 0, .y = 0, .width = 1000, .height = 800}}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/100);

  const GazeWindow window = reporter.Between(0, 1000, kCentre);

  EXPECT_EQ(window.gaze, Gaze::kThisScreen);
  EXPECT_EQ(window.content, ContentHit::kNo);
}

TEST(BetweenTest, NoContentAskedLeavesItOut) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  const ScreenContextReporter reporter(source, kSettings,
                                       /*titles_enabled=*/false);

  const GazeWindow window = reporter.Between(0, 1000, std::nullopt);

  EXPECT_EQ(window.gaze, Gaze::kUnknown);
  EXPECT_EQ(window.content, std::nullopt);
}

TEST(BetweenTest, ASampleInRangeGivesItsZone) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/500);

  const GazeWindow window = reporter.Between(0, 1000, std::nullopt);

  EXPECT_EQ(window.gaze, Gaze::kThisScreen);
}

TEST(BetweenTest, SamplesOutsideTheRangeAreIgnored) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  // Before the window's start and at or after its end: both sides of the
  // range check.
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/400);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/2000);

  const GazeWindow window = reporter.Between(500, 1000, std::nullopt);

  EXPECT_EQ(window.gaze, Gaze::kUnknown);
}

TEST(BetweenTest, TheMajorityZoneWins) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  const std::optional<HeadRotation> away = HeadRotation{.yaw_degrees = 90};
  reporter.Sample(away, kRest, std::nullopt, std::nullopt, /*at_ms=*/100);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/200);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/300);

  const GazeWindow window = reporter.Between(0, 1000, std::nullopt);

  EXPECT_EQ(window.gaze, Gaze::kThisScreen);
}

TEST(BetweenTest, OldSamplesPastMaxWindowMsAreForgotten) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  constexpr std::int64_t kShortMaxWindowMs = 100;
  ScreenContextReporter reporter(
      source, kSettings, /*titles_enabled=*/false, nullptr, EyeGains{},
      CalibrationSettings::kDefaultHeadOnlyYawDegrees, kShortMaxWindowMs);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/0);
  // 150ms later, more than max_window_ms past the first sample: it drops.
  reporter.Sample(std::nullopt, kRest, std::nullopt, std::nullopt,
                  /*at_ms=*/150);

  const GazeWindow window = reporter.Between(0, 200, std::nullopt);

  EXPECT_EQ(window.gaze, Gaze::kUnknown);
}

TEST(BetweenTest, TheMajorityContentHitWins) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/100);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/200);

  const GazeWindow window = reporter.Between(0, 1000, kCentre);

  EXPECT_EQ(window.content, ContentHit::kYes);
}

TEST(BetweenTest, SamplesWithNoFaceDoNotCountTowardContent) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  reporter.Sample(std::nullopt, kRest, std::nullopt, std::nullopt,
                  /*at_ms=*/100);

  const GazeWindow window = reporter.Between(0, 1000, kCentre);

  EXPECT_EQ(window.content, ContentHit::kUnknown);
}

TEST(BetweenTest, AContentHitOfNoIsCountedTowardTheMajority) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/100);

  const GazeWindow window = reporter.Between(0, 1000, kTopLeftCorner);

  EXPECT_EQ(window.content, ContentHit::kNo);
}

// A sample with gaze but no resting pose of its own (ScreenContextReader
// still learning, say) has an angle but no way to read ContentHitFrom's
// own cone from it: unknown, not yes or no.
TEST(BetweenTest, ASampleWithNoRestingPoseCountsAsUnknownContent) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  reporter.Sample(kRest, std::nullopt, std::nullopt, std::nullopt,
                  /*at_ms=*/100);

  const GazeWindow window = reporter.Between(0, 1000, kCentre);

  EXPECT_EQ(window.content, ContentHit::kUnknown);
}

// The EyeSignsBetween tests below stand for #145's and #165's own beat
// context: blink rate, a long blink and widened eyes over a window of
// Sample calls, fed alongside the same calls' own gaze (#142). None of
// this ever changes the generic feeling by rule
// (specs/FeelingBeat.tla's ContextNeverChangesFeeling, untouched).

constexpr EyeSignSettings kEyeSignSettings = {.elevated_blinks_per_minute = 14,
                                              .long_blink_ms = 500,
                                              .widened_eyes_threshold = 0.15,
                                              .widened_eyes_hold_ms = 200,
                                              .min_window_ms = 3000};

TEST(EyeSignsBetweenTest, NoSamplesInRangeIsUnknown) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  const ScreenContextReporter reporter(
      source, kSettings, /*titles_enabled=*/false, nullptr, EyeGains{},
      CalibrationSettings::kDefaultHeadOnlyYawDegrees,
      ScreenContextReporter::kDefaultMaxWindowMs, kEyeSignSettings);

  const EyeSigns signs = reporter.EyeSignsBetween(0, 1000);

  EXPECT_EQ(signs.blink_rate, BlinkRate::kUnknown);
  EXPECT_EQ(signs.long_blink, LongBlink::kUnknown);
  EXPECT_EQ(signs.widened_eyes, WidenedEyes::kUnknown);
}

// A frame with no face (eye_sign left at its default, nullopt) samples
// gaze as usual but contributes nothing of its own here.
TEST(EyeSignsBetweenTest, AFrameWithNoFaceContributesNoSample) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(
      source, kSettings, /*titles_enabled=*/false, nullptr, EyeGains{},
      CalibrationSettings::kDefaultHeadOnlyYawDegrees,
      ScreenContextReporter::kDefaultMaxWindowMs, kEyeSignSettings);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/100);

  const EyeSigns signs = reporter.EyeSignsBetween(0, 1000);

  EXPECT_EQ(signs.blink_rate, BlinkRate::kUnknown);
}

TEST(EyeSignsBetweenTest, SamplesOutsideTheRangeAreIgnored) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(
      source, kSettings, /*titles_enabled=*/false, nullptr, EyeGains{},
      CalibrationSettings::kDefaultHeadOnlyYawDegrees,
      ScreenContextReporter::kDefaultMaxWindowMs, kEyeSignSettings);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/400,
                  EyeSignSample{.blink = true, .eye_wide = 0.2});
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/2000,
                  EyeSignSample{.blink = true, .eye_wide = 0.2});

  const EyeSigns signs = reporter.EyeSignsBetween(500, 1000);

  EXPECT_EQ(signs.blink_rate, BlinkRate::kUnknown);
  EXPECT_EQ(signs.widened_eyes, WidenedEyes::kUnknown);
}

// Each eye_sign's own timestamp_ms is replaced with Sample's own at_ms
// (the wall clock), whatever it carried in, so EyeSignsBetween's bounds
// agree with it.
TEST(EyeSignsBetweenTest, EyeSignTimestampsAreReplacedWithAtMs) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(
      source, kSettings, /*titles_enabled=*/false, nullptr, EyeGains{},
      CalibrationSettings::kDefaultHeadOnlyYawDegrees,
      ScreenContextReporter::kDefaultMaxWindowMs, kEyeSignSettings);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/500,
                  EyeSignSample{.timestamp_ms = 999999, .blink = true});

  const EyeSigns signs = reporter.EyeSignsBetween(0, 1000);

  EXPECT_EQ(signs.long_blink, LongBlink::kNo);
}

TEST(EyeSignsBetweenTest, OldSamplesPastMaxWindowMsAreForgotten) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  constexpr std::int64_t kShortMaxWindowMs = 100;
  ScreenContextReporter reporter(
      source, kSettings, /*titles_enabled=*/false, nullptr, EyeGains{},
      CalibrationSettings::kDefaultHeadOnlyYawDegrees, kShortMaxWindowMs,
      kEyeSignSettings);
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/0,
                  EyeSignSample{.eye_wide = 0.2});
  // 150ms later, more than max_window_ms past the first sample: it drops,
  // the same way an old GazeSample does (BetweenTest's own
  // OldSamplesPastMaxWindowMsAreForgotten).
  reporter.Sample(std::nullopt, kRest, std::nullopt, std::nullopt,
                  /*at_ms=*/150);

  const EyeSigns signs = reporter.EyeSignsBetween(0, 200);

  EXPECT_EQ(signs.widened_eyes, WidenedEyes::kUnknown);
}

// A run of elevated-looking blinks, each its own Sample call, read the
// same way EyeSignsOfTest's own synthetic samples are (src/eye_signs.h);
// this is only proving the buffer feeds EyeSignsOf at all, not
// reproving its own rule.
TEST(EyeSignsBetweenTest, SamplesInRangeAreClassifiedByEyeSignsOf) {
  MockScreenSource source;
  EXPECT_CALL(source, Screens())
      .WillRepeatedly(Return(std::vector<ScreenRect>{kMain}));
  ScreenContextReporter reporter(
      source, kSettings, /*titles_enabled=*/false, nullptr, EyeGains{},
      CalibrationSettings::kDefaultHeadOnlyYawDegrees,
      ScreenContextReporter::kDefaultMaxWindowMs, kEyeSignSettings);
  // Four separate one-sample blink runs over 10 s (each closed by a
  // following false sample, as a real blink's own next frame would):
  // 24 a minute, past kEyeSignSettings' own elevated_blinks_per_minute
  // (14), the same math
  // EyeSignsOfTest.ManyBlinksOverTheSameWindowIsElevated already proves.
  for (const std::int64_t at_ms : {0, 2500, 5000, 7500}) {
    reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, at_ms,
                    EyeSignSample{.blink = true});
    reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, at_ms + 66,
                    EyeSignSample{.blink = false});
  }
  reporter.Sample(kRest, kRest, std::nullopt, std::nullopt, /*at_ms=*/10000,
                  EyeSignSample{.blink = false});

  const EyeSigns signs = reporter.EyeSignsBetween(0, 10001);

  EXPECT_EQ(signs.blink_rate, BlinkRate::kElevated);
}

TEST(ScreenContextReporterTest, SetProfileSwapsWhatNextCallReads) {
  MockScreenSource source;
  EXPECT_CALL(source, CurrentApp(false))
      .Times(2)
      .WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(source, Mouse()).Times(2).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(source, Screens())
      .Times(2)
      .WillRepeatedly(Return(std::vector<ScreenRect>{}));
  ScreenContextReporter reporter(source, kSettings, /*titles_enabled=*/false);
  EXPECT_EQ(reporter.Json(kRest, kRest), R"({"gaze":"this_screen"})");

  const CalibrationProfile profile = TwoScreenProfile();
  reporter.SetProfile(&profile);

  EXPECT_EQ(reporter.Json(kRest, kRest),
            R"({"gaze":"this_screen","calibrated_screen":0})");
}

}  // namespace
}  // namespace facepalm
