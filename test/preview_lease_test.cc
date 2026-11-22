#include "src/preview_lease.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Each test stands for the specs/PreviewLease.tla property or action it is
// named for.

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class PreviewLeaseTest : public ::testing::Test {
 protected:
  std::vector<bool> changes_;
  PreviewLease lease_{
      [this](bool streaming) { changes_.push_back(streaming); }};
};

using StartTest = PreviewLeaseTest;

TEST_F(StartTest, NeedsTheCameraOpen) {
  EXPECT_FALSE(lease_.Start());
  EXPECT_FALSE(lease_.IsStreaming());
  EXPECT_THAT(changes_, IsEmpty());
}

TEST_F(StartTest, StreamsOnceTheCameraIsOpen) {
  lease_.CameraOpened();
  EXPECT_TRUE(lease_.Start());
  EXPECT_TRUE(lease_.IsStreaming());
  EXPECT_THAT(changes_, ElementsAre(true));
}

TEST_F(StartTest, AskingAgainWhileAlreadyStreamingChangesNothing) {
  lease_.CameraOpened();
  ASSERT_TRUE(lease_.Start());
  changes_.clear();

  EXPECT_TRUE(lease_.Start());
  EXPECT_THAT(changes_, IsEmpty());
}

TEST_F(StartTest,
       AnotherCameraOpenedWithNoCloseFirstStillEndsAnyActivePreview) {
  // Defensive: a second CameraOpened without a CameraClosed in between
  // should never happen in practice (DaemonAdmission.tla's own open/close
  // already alternate), but if it did, the stale grant must not survive.
  lease_.CameraOpened();
  ASSERT_TRUE(lease_.Start());
  changes_.clear();

  lease_.CameraOpened();

  EXPECT_FALSE(lease_.IsStreaming());
  EXPECT_THAT(changes_, ElementsAre(false));
}

using StopTest = PreviewLeaseTest;

TEST_F(StopTest, EndsAnActivePreview) {
  lease_.CameraOpened();
  lease_.Start();
  changes_.clear();

  lease_.Stop();

  EXPECT_FALSE(lease_.IsStreaming());
  EXPECT_THAT(changes_, ElementsAre(false));
}

TEST_F(StopTest, WithNoPreviewChangesNothing) {
  lease_.Stop();
  EXPECT_THAT(changes_, IsEmpty());
}

using NeverStreamsWithoutCameraTest = PreviewLeaseTest;

TEST_F(NeverStreamsWithoutCameraTest, ClosingTheCameraEndsAnActivePreview) {
  lease_.CameraOpened();
  lease_.Start();
  changes_.clear();

  lease_.CameraClosed();

  EXPECT_FALSE(lease_.IsStreaming());
  EXPECT_THAT(changes_, ElementsAre(false));
}

TEST_F(NeverStreamsWithoutCameraTest, ClosingWithNoPreviewChangesNothing) {
  lease_.CameraOpened();
  lease_.CameraClosed();
  EXPECT_THAT(changes_, IsEmpty());
}

using NeverStreamsOnAStaleRequestTest = PreviewLeaseTest;

// GuardStartNeedsCamera: a grant from before the camera closed does not
// carry over once it reopens; Start must be asked again.
TEST_F(NeverStreamsOnAStaleRequestTest, AReopenedCameraNeedsANewStart) {
  lease_.CameraOpened();
  ASSERT_TRUE(lease_.Start());
  lease_.CameraClosed();
  lease_.CameraOpened();

  EXPECT_FALSE(lease_.IsStreaming());

  EXPECT_TRUE(lease_.Start());
  EXPECT_TRUE(lease_.IsStreaming());
}

}  // namespace
}  // namespace facepalm
