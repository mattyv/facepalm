#include "src/daemon_engine_support.h"

#include <memory>
#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/calibration.h"
#include "src/config.h"
#include "src/user_paths.h"

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Ne;
using ::testing::NotNull;

TEST(PathOrEmptyTest, ReadsTheNamedMemberWhenPathsAreResolved) {
  UserPaths paths;
  paths.preview_request = "/home/user/.local/state/facepalm/preview_request";
  EXPECT_THAT(PathOrEmpty(paths, &UserPaths::preview_request),
              Eq(paths.preview_request));
}

TEST(PathOrEmptyTest, ReadsADifferentMemberToo) {
  UserPaths paths;
  paths.calibration = "/home/user/.local/state/facepalm/calibration";
  EXPECT_THAT(PathOrEmpty(paths, &UserPaths::calibration),
              Eq(paths.calibration));
}

TEST(PathOrEmptyTest, IsEmptyWithNoPathsResolved) {
  EXPECT_THAT(PathOrEmpty(std::nullopt, &UserPaths::preview_request),
              IsEmpty());
}

TEST(CalibrationSettingsFromConfigTest, MapsEveryField) {
  Config config;
  config.eye_gaze_horizontal_gain = 11;
  config.eye_gaze_vertical_gain = 22;
  config.calibration_min_head_travel_degrees = 33;
  config.calibration_min_axis_degrees = 44;
  config.calibration_min_spread_degrees = 55;
  config.eye_gaze_head_only_yaw_degrees = 66;

  const CalibrationSettings settings = CalibrationSettingsFromConfig(config);
  EXPECT_THAT(settings.default_gains.horizontal, Eq(11));
  EXPECT_THAT(settings.default_gains.vertical, Eq(22));
  EXPECT_THAT(settings.min_head_travel_degrees, Eq(33));
  EXPECT_THAT(settings.min_axis_degrees, Eq(44));
  EXPECT_THAT(settings.min_spread_degrees, Eq(55));
  EXPECT_THAT(settings.head_only_yaw_degrees, Eq(66));
}

TEST(SharedCalibrationProfileTest, NullWithNoProfileGiven) {
  EXPECT_THAT(SharedCalibrationProfile(nullptr), IsNull());
}

TEST(SharedCalibrationProfileTest, CopiesTheProfileGiven) {
  CalibrationProfile profile;
  profile.gains.horizontal = 7;

  const std::shared_ptr<const CalibrationProfile> shared =
      SharedCalibrationProfile(&profile);

  ASSERT_THAT(shared, NotNull());
  EXPECT_THAT(shared->gains.horizontal, Eq(7));
  EXPECT_THAT(shared.get(), Ne(&profile));  // a copy, not a borrow
}

}  // namespace
}  // namespace facepalm
