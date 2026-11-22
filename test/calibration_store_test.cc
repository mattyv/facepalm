#include "src/calibration_store.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/calibration.h"
#include "src/eye_gaze.h"

// GoogleTest assertions check each optional before access.
// NOLINTBEGIN(bugprone-unchecked-optional-access)
namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Return;

CalibrationProfile SampleProfile() {
  CalibrationProfile profile;
  profile.resting = {.yaw_degrees = 1, .pitch_degrees = -2, .roll_degrees = 3};
  profile.resting_eye = {.horizontal = 0.1, .vertical = -0.2};
  profile.gains = {.horizontal = 25, .vertical = 32};
  ScreenCalibration screen;
  double value = 10;
  for (const CalibrationPoint point : kCalibrationPoints) {
    screen.points[static_cast<std::size_t>(point)] = {
        .yaw_degrees = value,
        .pitch_degrees = value + 1,
        .roll_degrees = value + 2};
    value += 10;
  }
  profile.screens = {screen};
  return profile;
}

// Checks every point of the round trip together: the resting pose, and
// each of a screen's five points.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SerializeCalibrationProfileTest, RoundTripsThroughParse) {
  const CalibrationProfile original = SampleProfile();

  const std::string text = SerializeCalibrationProfile(original);
  std::string error;
  const std::optional<CalibrationProfile> parsed =
      ParseCalibrationProfile(text, error);

  ASSERT_TRUE(parsed.has_value()) << error;
  EXPECT_THAT(parsed->resting.yaw_degrees, Eq(original.resting.yaw_degrees));
  EXPECT_THAT(parsed->resting.pitch_degrees,
              Eq(original.resting.pitch_degrees));
  EXPECT_THAT(parsed->resting.roll_degrees, Eq(original.resting.roll_degrees));
  EXPECT_THAT(parsed->resting_eye.horizontal,
              Eq(original.resting_eye.horizontal));
  EXPECT_THAT(parsed->resting_eye.vertical, Eq(original.resting_eye.vertical));
  EXPECT_THAT(parsed->gains.horizontal, Eq(original.gains.horizontal));
  EXPECT_THAT(parsed->gains.vertical, Eq(original.gains.vertical));
  ASSERT_THAT(parsed->screens.size(), Eq(1U));
  for (const CalibrationPoint point : kCalibrationPoints) {
    const auto index = static_cast<std::size_t>(point);
    EXPECT_THAT(parsed->screens[0].points[index].yaw_degrees,
                Eq(original.screens[0].points[index].yaw_degrees));
    EXPECT_THAT(parsed->screens[0].points[index].pitch_degrees,
                Eq(original.screens[0].points[index].pitch_degrees));
  }
}

// Every field SerializeCalibrationProfile writes before "screens = N"
// (#137's resting_eye and gains), with zero values: the shared prefix the
// "past resting" tests below build on, so only their own missing field is
// what's missing.
constexpr std::string_view kRestingAndGainsText =
    "version = 2\nresting.yaw = 0\nresting.pitch = 0\nresting.roll = 0\n"
    "resting_eye.horizontal = 0\nresting_eye.vertical = 0\n"
    "gains.horizontal = 0\ngains.vertical = 0\n";

TEST(ParseCalibrationProfileTest, RefusesAWrongVersion) {
  std::string error;
  EXPECT_THAT(ParseCalibrationProfile("version = 3\n", error),
              Eq(std::nullopt));
  EXPECT_THAT(error, Eq("unsupported calibration file version"));
}

TEST(ParseCalibrationProfileTest, RefusesANonNumericVersion) {
  std::string error;
  EXPECT_THAT(ParseCalibrationProfile("version = x\n", error),
              Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected version = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesAWholeNumberWithTrailingGarbage) {
  std::string error;
  EXPECT_THAT(ParseCalibrationProfile("version = 2x\n", error),
              Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected version = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesAMissingField) {
  std::string error;
  EXPECT_THAT(ParseCalibrationProfile("version = 2\n", error),
              Eq(std::nullopt));
  EXPECT_THAT(error, Not(IsEmpty()));
}

TEST(ParseCalibrationProfileTest, RefusesAWrongKeyWhereOneIsExpected) {
  std::string error;
  const std::string text = "version = 2\nsomething.else = 1\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected resting.yaw = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesAMalformedNumber) {
  std::string error;
  const std::string text =
      "version = 2\nresting.yaw = x\nresting.pitch = 0\nresting.roll = 0\n"
      "screens = 1\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected resting.yaw = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesANumberWithTrailingGarbage) {
  std::string error;
  const std::string text =
      "version = 2\nresting.yaw = 5x\nresting.pitch = 0\nresting.roll = 0\n"
      "screens = 1\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected resting.yaw = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesAMissingRestingEyeField) {
  std::string error;
  const std::string text =
      "version = 2\nresting.yaw = 0\nresting.pitch = 0\nresting.roll = 0\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected resting_eye.horizontal = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesAMissingGainsField) {
  std::string error;
  const std::string text = "version = 2\nresting.yaw = 0\nresting.pitch = 0\n"
                           "resting.roll = 0\nresting_eye.horizontal = 0\n"
                           "resting_eye.vertical = 0\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected gains.horizontal = a number"));
}

TEST(ParseCalibrationProfileTest, ParsesAFileMissingItsFinalNewline) {
  std::string text = SerializeCalibrationProfile(SampleProfile());
  ASSERT_THAT(text.back(), Eq('\n'));
  text.pop_back();  // The last point's roll has no trailing newline.

  std::string error;
  EXPECT_THAT(ParseCalibrationProfile(text, error).has_value(), Eq(true))
      << error;
}

TEST(ParseCalibrationProfileTest, RefusesAMissingScreensField) {
  std::string error;
  const std::string text(kRestingAndGainsText);
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected screens = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesTooFewOrTooManyScreens) {
  std::string error;
  const std::string text = std::string(kRestingAndGainsText) + "screens = 0\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error,
              Eq("calibration file names an impossible number of screens"));

  std::string high_error;
  const std::string high_text =
      std::string(kRestingAndGainsText) + "screens = 9\n";
  EXPECT_THAT(ParseCalibrationProfile(high_text, high_error), Eq(std::nullopt));
  EXPECT_THAT(high_error,
              Eq("calibration file names an impossible number of screens"));
}

TEST(ParseCalibrationProfileTest, RefusesAScreenMissingItsFirstPoint) {
  std::string error;
  const std::string text = std::string(kRestingAndGainsText) + "screens = 1\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected screen.0.centre.yaw = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesAScreenMissingItsPitch) {
  std::string error;
  const std::string text = std::string(kRestingAndGainsText) +
                           "screens = 1\nscreen.0.centre.yaw = 0\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected screen.0.centre.pitch = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesAScreenMissingItsRoll) {
  std::string error;
  const std::string text =
      std::string(kRestingAndGainsText) +
      "screens = 1\nscreen.0.centre.yaw = 0\nscreen.0.centre.pitch = 0\n";
  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("expected screen.0.centre.roll = a number"));
}

TEST(ParseCalibrationProfileTest, RefusesTrailingData) {
  std::string error;
  std::string text = SerializeCalibrationProfile(SampleProfile());
  text += "extra = 1\n";

  EXPECT_THAT(ParseCalibrationProfile(text, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("calibration file has trailing data"));
}

class MockCalibrationFileIo : public CalibrationFileIo {
 public:
  MOCK_METHOD(int, OpenTemp, (const std::string&), (override));
  MOCK_METHOD(ssize_t, Write, (int, const void*, std::size_t), (override));
  MOCK_METHOD(int, Sync, (int), (override));
  MOCK_METHOD(void, Close, (int), (override));
  MOCK_METHOD(int, Rename, (const std::string&), (override));
  MOCK_METHOD(int, OpenDirectory, (const std::string&), (override));
  MOCK_METHOD(int, OpenForRead, (const std::string&), (override));
  MOCK_METHOD(int, Stat, (int, struct stat&), (override));
  MOCK_METHOD(uid_t, User, (), (override));
  MOCK_METHOD(ssize_t, Read, (int, void*, std::size_t), (override));
};

class CalibrationFileStoreIoTest : public ::testing::Test {
 protected:
  std::shared_ptr<NiceMock<MockCalibrationFileIo>> io_ =
      std::make_shared<NiceMock<MockCalibrationFileIo>>();
  CalibrationFileStore store_{"/private/profile", io_};
};

TEST_F(CalibrationFileStoreIoTest, SavesByWritingSyncingAndRenaming) {
  EXPECT_CALL(*io_, OpenTemp("/private/profile")).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write(7, testing::_, testing::_))
      .WillOnce([](int, const void*, std::size_t size) {
        return static_cast<ssize_t>(size);
      });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Close(7));
  EXPECT_CALL(*io_, Rename("/private/profile")).WillOnce(Return(0));
  EXPECT_CALL(*io_, OpenDirectory("/private")).WillOnce(Return(8));
  EXPECT_CALL(*io_, Sync(8)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Close(8));

  std::string error;
  EXPECT_TRUE(store_.Save(SampleProfile(), error));
  EXPECT_THAT(error, IsEmpty());
}

TEST_F(CalibrationFileStoreIoTest, WritesInMoreThanOneCall) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write(7, testing::_, testing::_))
      .WillOnce(Return(1))
      .WillOnce([](int, const void*, std::size_t size) {
        return static_cast<ssize_t>(size);
      });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Rename).WillOnce(Return(0));
  EXPECT_CALL(*io_, OpenDirectory).WillOnce(Return(8));
  EXPECT_CALL(*io_, Sync(8)).WillOnce(Return(0));

  std::string error;
  EXPECT_TRUE(store_.Save(SampleProfile(), error));
}

// SyncParent's slash-finding (src/calibration_store.cc), through the paths
// it can see: a name with no directory, the filesystem root, and an
// ordinary nested path.
TEST_F(CalibrationFileStoreIoTest, SyncsTheRootWhenThePathIsAtItsTopLevel) {
  CalibrationFileStore root_store("/profile", io_);
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write).WillOnce([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Rename).WillOnce(Return(0));
  EXPECT_CALL(*io_, OpenDirectory("/")).WillOnce(Return(8));
  EXPECT_CALL(*io_, Sync(8)).WillOnce(Return(0));

  std::string error;
  EXPECT_TRUE(root_store.Save(SampleProfile(), error));
}

TEST_F(CalibrationFileStoreIoTest, SyncsTheCurrentDirectoryWhenThereIsNoSlash) {
  CalibrationFileStore relative_store("profile", io_);
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write).WillOnce([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Rename).WillOnce(Return(0));
  EXPECT_CALL(*io_, OpenDirectory(".")).WillOnce(Return(8));
  EXPECT_CALL(*io_, Sync(8)).WillOnce(Return(0));

  std::string error;
  EXPECT_TRUE(relative_store.Save(SampleProfile(), error));
}

TEST_F(CalibrationFileStoreIoTest, RefusesWhenTheFileCannotBeOpened) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(-1));

  std::string error;
  EXPECT_FALSE(store_.Save(SampleProfile(), error));
  EXPECT_THAT(error, Eq("cannot open calibration file"));
}

TEST_F(CalibrationFileStoreIoTest, RefusesAFailedWrite) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write).WillOnce(Return(-1));
  EXPECT_CALL(*io_, Close(7));

  std::string error;
  EXPECT_FALSE(store_.Save(SampleProfile(), error));
  EXPECT_THAT(error, Eq("cannot write calibration file"));
}

TEST_F(CalibrationFileStoreIoTest, RefusesAFailedSync) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write).WillOnce([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(-1));
  EXPECT_CALL(*io_, Close(7));

  std::string error;
  EXPECT_FALSE(store_.Save(SampleProfile(), error));
  EXPECT_THAT(error, Eq("cannot save calibration file"));
}

TEST_F(CalibrationFileStoreIoTest, RefusesAFailedRename) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write).WillOnce([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Rename).WillOnce(Return(-1));

  std::string error;
  EXPECT_FALSE(store_.Save(SampleProfile(), error));
  EXPECT_THAT(error, Eq("cannot save calibration file"));
}

TEST_F(CalibrationFileStoreIoTest,
       RefusesWhenTheParentDirectoryCannotBeOpened) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write).WillOnce([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Rename).WillOnce(Return(0));
  EXPECT_CALL(*io_, OpenDirectory).WillOnce(Return(-1));

  std::string error;
  EXPECT_FALSE(store_.Save(SampleProfile(), error));
  EXPECT_THAT(error, Eq("cannot save calibration file"));
}

TEST_F(CalibrationFileStoreIoTest, RefusesAFailedParentSync) {
  EXPECT_CALL(*io_, OpenTemp).WillOnce(Return(7));
  EXPECT_CALL(*io_, Write).WillOnce([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  EXPECT_CALL(*io_, Sync(7)).WillOnce(Return(0));
  EXPECT_CALL(*io_, Rename).WillOnce(Return(0));
  EXPECT_CALL(*io_, OpenDirectory).WillOnce(Return(8));
  EXPECT_CALL(*io_, Sync(8)).WillOnce(Return(-1));

  std::string error;
  EXPECT_FALSE(store_.Save(SampleProfile(), error));
  EXPECT_THAT(error, Eq("cannot save calibration file"));
}

TEST(LoadCalibrationProfileIoTest, ReturnsNulloptWithNoErrorWhenThereIsNoFile) {
  const auto io = std::make_shared<NiceMock<MockCalibrationFileIo>>();
  EXPECT_CALL(*io, OpenForRead).WillOnce(Return(-1));

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile("path", error, io), Eq(std::nullopt));
  EXPECT_THAT(error, IsEmpty());
}

TEST(LoadCalibrationProfileIoTest, RefusesWhenStatFails) {
  const auto io = std::make_shared<NiceMock<MockCalibrationFileIo>>();
  EXPECT_CALL(*io, OpenForRead).WillOnce(Return(7));
  EXPECT_CALL(*io, Stat).WillOnce(Return(-1));
  EXPECT_CALL(*io, Close(7));

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile("path", error, io), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("calibration file is unsafe to read"));
}

TEST(LoadCalibrationProfileIoTest, RefusesANonRegularFile) {
  const auto io = std::make_shared<NiceMock<MockCalibrationFileIo>>();
  EXPECT_CALL(*io, OpenForRead).WillOnce(Return(7));
  EXPECT_CALL(*io, Stat).WillOnce([](int, struct stat& info) {
    info.st_mode = S_IFDIR | 0600;
    return 0;
  });
  EXPECT_CALL(*io, Close(7));

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile("path", error, io), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("calibration file is unsafe to read"));
}

TEST(LoadCalibrationProfileIoTest, RefusesAFileOwnedBySomeoneElse) {
  const auto io = std::make_shared<NiceMock<MockCalibrationFileIo>>();
  EXPECT_CALL(*io, OpenForRead).WillOnce(Return(7));
  EXPECT_CALL(*io, Stat).WillOnce([](int, struct stat& info) {
    info.st_mode = S_IFREG | 0600;
    info.st_uid = 1;
    return 0;
  });
  EXPECT_CALL(*io, User).WillOnce(Return(2));
  EXPECT_CALL(*io, Close(7));

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile("path", error, io), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("calibration file is unsafe to read"));
}

TEST(LoadCalibrationProfileIoTest, RefusesAFileReadableByOthers) {
  const auto io = std::make_shared<NiceMock<MockCalibrationFileIo>>();
  EXPECT_CALL(*io, OpenForRead).WillOnce(Return(7));
  EXPECT_CALL(*io, Stat).WillOnce([](int, struct stat& info) {
    info.st_mode = S_IFREG | 0644;
    info.st_uid = 1;
    return 0;
  });
  EXPECT_CALL(*io, User).WillOnce(Return(1));
  EXPECT_CALL(*io, Close(7));

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile("path", error, io), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("calibration file is unsafe to read"));
}

// Readying a safe file to read (OpenForRead, Stat and User all succeed),
// so each of these tests need only set Read's behaviour.
std::shared_ptr<NiceMock<MockCalibrationFileIo>> ReadableFileIo() {
  const auto io = std::make_shared<NiceMock<MockCalibrationFileIo>>();
  ON_CALL(*io, OpenForRead).WillByDefault(Return(7));
  ON_CALL(*io, Stat).WillByDefault([](int, struct stat& info) {
    info.st_mode = S_IFREG | 0600;
    return 0;
  });
  ON_CALL(*io, User).WillByDefault(Return(0));
  return io;
}

TEST(LoadCalibrationProfileIoTest, RefusesAReadFailure) {
  const auto io = ReadableFileIo();
  EXPECT_CALL(*io, Read).WillOnce([](int, void*, std::size_t) {
    errno = EIO;
    return -1;
  });

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile("path", error, io), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("cannot read calibration file"));
}

// ReadWholeFile's retry on EINTR, as ClientToken's LoadClientToken is.
TEST(LoadCalibrationProfileIoTest, RetriesAnInterruptedRead) {
  const auto io = ReadableFileIo();
  const std::string text = SerializeCalibrationProfile(SampleProfile());
  EXPECT_CALL(*io, Read)
      .WillOnce([](int, void*, std::size_t) {
        errno = EINTR;
        return -1;
      })
      .WillOnce([&](int, void* buffer, std::size_t size) {
        const std::size_t copied = std::min(size, text.size());
        std::memcpy(buffer, text.data(), copied);
        return static_cast<ssize_t>(copied);
      })
      .WillOnce(Return(0));

  std::string error;
  const std::optional<CalibrationProfile> profile =
      LoadCalibrationProfile("path", error, io);
  ASSERT_TRUE(profile.has_value()) << error;
}

TEST(LoadCalibrationProfileIoTest, RefusesAFileTooLargeToBeAProfile) {
  const auto io = ReadableFileIo();
  EXPECT_CALL(*io, Read).WillRepeatedly(
      [](int, void* buffer, std::size_t size) {
        std::memset(buffer, 'a', size);
        return static_cast<ssize_t>(size);
      });

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile("path", error, io), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("cannot read calibration file"));
}

TEST(LoadCalibrationProfileIoTest, ParsesTheContentsOfASafeFile) {
  const auto io = ReadableFileIo();
  const std::string text = SerializeCalibrationProfile(SampleProfile());
  EXPECT_CALL(*io, Read)
      .WillOnce([&](int, void* buffer, std::size_t size) {
        const std::size_t copied = std::min(size, text.size());
        std::memcpy(buffer, text.data(), copied);
        return static_cast<ssize_t>(copied);
      })
      .WillOnce(Return(0));

  std::string error;
  const std::optional<CalibrationProfile> profile =
      LoadCalibrationProfile("path", error, io);
  ASSERT_TRUE(profile.has_value()) << error;
  EXPECT_THAT(profile->screens.size(), Eq(1U));
}

// Round-trips through real files, exercising the real POSIX Io (#36, #35).
class CalibrationStoreFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::array<char, 40> path = {"/tmp/fp-calib-XXXXXX"};
    const int fd = mkstemp(path.data());
    ASSERT_GE(fd, 0);
    close(fd);
    std::remove(path.data());  // Save creates it; Load should see none yet.
    path_ = path.data();
  }
  void TearDown() override {
    std::remove(path_.c_str());
    std::remove((path_ + ".tmp").c_str());
  }
  std::string path_;
};

TEST_F(CalibrationStoreFileTest, SavesAndLoadsAPrivateFile) {
  std::string load_error;
  EXPECT_THAT(LoadCalibrationProfile(path_, load_error), Eq(std::nullopt));
  EXPECT_THAT(load_error, IsEmpty());

  CalibrationFileStore store(path_);
  std::string save_error;
  ASSERT_TRUE(store.Save(SampleProfile(), save_error)) << save_error;

  struct stat info{};
  ASSERT_EQ(stat(path_.c_str(), &info), 0);
  EXPECT_THAT(info.st_mode & 0777U, Eq(0600));

  std::string error;
  const std::optional<CalibrationProfile> loaded =
      LoadCalibrationProfile(path_, error);
  ASSERT_TRUE(loaded.has_value()) << error;
  EXPECT_THAT(loaded->screens.size(), Eq(1U));

  // Saving again replaces it atomically, leaving no stray temp file.
  ASSERT_TRUE(store.Save(SampleProfile(), save_error)) << save_error;
  struct stat temp_info{};
  EXPECT_NE(stat((path_ + ".tmp").c_str(), &temp_info), 0);
}

TEST_F(CalibrationStoreFileTest, RefusesAFileReadableByTheGroup) {
  CalibrationFileStore store(path_);
  std::string save_error;
  ASSERT_TRUE(store.Save(SampleProfile(), save_error)) << save_error;
  ASSERT_EQ(chmod(path_.c_str(), 0640), 0);

  std::string error;
  EXPECT_THAT(LoadCalibrationProfile(path_, error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("calibration file is unsafe to read"));
}

TEST_F(CalibrationStoreFileTest, RefusesToSaveWhenTheDirectoryDoesNotExist) {
  CalibrationFileStore store(path_ + "/missing/profile");
  std::string error;
  EXPECT_FALSE(store.Save(SampleProfile(), error));
  EXPECT_THAT(error, Not(IsEmpty()));
}

// Acceptance (item 12 of #61, #36, #35): a session walked through fixture
// head poses, saved to a real file and loaded back, maps a pose to the
// right screen where the uncalibrated guess that two screens sit evenly
// either side of the camera's forward axis could not. A camera off to one
// side gives exactly this: screen 0's centre is 50 degrees of yaw to the
// left, screen 1's only 10 to the right, not symmetric around 0.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(CalibrationStoreFileTest,
       ACalibratedProfileLocatesAScreenAnUncalibratedGuessMisses) {
  CalibrationFileStore store(path_);
  CalibrationSession session(store);

  std::string error;
  ASSERT_TRUE(session.Start(2, error).has_value()) << error;
  // The resting pose, straight ahead.
  session.Update({.yaw_degrees = 0}, EyeSignal{});
  ASSERT_TRUE(session.Next(error).has_value()) << error;
  // The gains step (#137): a real fixate-and-turn, well past every
  // default threshold.
  for (int step = -10; step <= 10; step += 2) {
    session.Update({.yaw_degrees = static_cast<double>(step)},
                   {.horizontal = -step / 20.0});
  }
  ASSERT_TRUE(session.Next(error).has_value()) << error;
  const std::array<double, 2> screen_centres = {-50, 10};
  for (const double centre : screen_centres) {
    // Each corner sits clearly apart from its own screen's centre
    // (#137's spread check), without changing which centre
    // NearestCalibratedScreen is tested against below.
    for (const double offset : {0.0, 20.0, -20.0, 25.0, -25.0}) {
      session.Update({.yaw_degrees = centre + offset}, EyeSignal{});
      const auto status = session.Next(error);
      ASSERT_TRUE(status.has_value()) << error;
    }
  }

  std::string load_error;
  const std::optional<CalibrationProfile> profile =
      LoadCalibrationProfile(path_, load_error);
  ASSERT_TRUE(profile.has_value()) << load_error;

  // A pose 5 degrees left of the camera's forward axis. An uncalibrated
  // guess splitting the screens evenly either side of 0 picks screen 0,
  // but the pose is 45 degrees from screen 0's real centre and only 15
  // from screen 1's: the calibrated profile picks screen 1 correctly.
  const HeadRotation pose = {.yaw_degrees = -5};
  const int uncalibrated_guess = pose.yaw_degrees < 0 ? 0 : 1;
  ASSERT_THAT(uncalibrated_guess, Eq(0)) << "the guess this test contrasts";

  EXPECT_THAT(NearestCalibratedScreen(*profile, pose), Eq(1U));
}

}  // namespace
}  // namespace facepalm
// NOLINTEND(bugprone-unchecked-optional-access)
