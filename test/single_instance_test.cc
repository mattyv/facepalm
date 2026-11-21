#include "src/single_instance.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// The NoInstance*, AnInstance*, ALostHandoff* and GivesUp* tests stand for
// specs/SingleInstance.tla's AtMostOnePrimary, LockMatchesPrimary,
// LaunchSettles and RequestServed.

namespace facepalm {
namespace {

using ::testing::InSequence;
using ::testing::Return;

class MockSingleInstanceIo : public SingleInstanceIo {
 public:
  MOCK_METHOD(bool, TryLock, (), (override));
  MOCK_METHOD(bool, RequestSettings, (), (override));
};

TEST(ClaimSingleInstanceTest, NoInstanceClaimsTheLockAndBecomesPrimary) {
  MockSingleInstanceIo io;
  EXPECT_CALL(io, TryLock).WillOnce(Return(true));
  EXPECT_CALL(io, RequestSettings).Times(0);

  EXPECT_EQ(ClaimSingleInstance(/*wants_settings=*/true, io),
            SingleInstanceRole::kPrimary);
}

TEST(ClaimSingleInstanceTest, PlainLaunchWithNoSettingsFlagJustExits) {
  MockSingleInstanceIo io;
  EXPECT_CALL(io, TryLock).WillOnce(Return(false));
  EXPECT_CALL(io, RequestSettings).Times(0);

  EXPECT_EQ(ClaimSingleInstance(/*wants_settings=*/false, io),
            SingleInstanceRole::kForwarded);
}

TEST(ClaimSingleInstanceTest, AnInstanceWithSettingsForwardsToTheRunningOne) {
  MockSingleInstanceIo io;
  EXPECT_CALL(io, TryLock).WillOnce(Return(false));
  EXPECT_CALL(io, RequestSettings).WillOnce(Return(true));

  EXPECT_EQ(ClaimSingleInstance(/*wants_settings=*/true, io),
            SingleInstanceRole::kForwarded);
}

TEST(ClaimSingleInstanceTest, ALostHandoffRetriesAndClaimsTheLock) {
  MockSingleInstanceIo io;
  {
    InSequence sequence;
    EXPECT_CALL(io, TryLock).WillOnce(Return(false));
    EXPECT_CALL(io, RequestSettings).WillOnce(Return(false));
    EXPECT_CALL(io, TryLock).WillOnce(Return(true));
  }

  EXPECT_EQ(ClaimSingleInstance(/*wants_settings=*/true, io),
            SingleInstanceRole::kPrimary);
}

TEST(ClaimSingleInstanceTest, ALostHandoffRetriesAndForwardsToTheNextOwner) {
  MockSingleInstanceIo io;
  {
    InSequence sequence;
    EXPECT_CALL(io, TryLock).WillOnce(Return(false));
    EXPECT_CALL(io, RequestSettings).WillOnce(Return(false));
    EXPECT_CALL(io, TryLock).WillOnce(Return(false));
    EXPECT_CALL(io, RequestSettings).WillOnce(Return(true));
  }

  EXPECT_EQ(ClaimSingleInstance(/*wants_settings=*/true, io),
            SingleInstanceRole::kForwarded);
}

TEST(ClaimSingleInstanceTest, GivesUpAfterRepeatedLostHandoffs) {
  MockSingleInstanceIo io;
  EXPECT_CALL(io, TryLock)
      .Times(kMaxSingleInstanceAttempts)
      .WillRepeatedly(Return(false));
  EXPECT_CALL(io, RequestSettings)
      .Times(kMaxSingleInstanceAttempts)
      .WillRepeatedly(Return(false));

  EXPECT_EQ(ClaimSingleInstance(/*wants_settings=*/true, io),
            SingleInstanceRole::kForwarded);
}

}  // namespace
}  // namespace facepalm
