#include "src/screen_context_reporter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "src/calibration.h"
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

}  // namespace
}  // namespace facepalm
