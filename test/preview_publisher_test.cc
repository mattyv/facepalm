#include "src/preview_publisher.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/frame.h"
#include "src/preview_lease.h"

// PreviewLease is simple, deterministic and already covered by its own
// test (test/preview_lease_test.cc); this runs it for real, as
// tuning_test.cc runs the feelings rule for real, rather than mocking it.

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr Frame kFrame{};

class PreviewPublisherTest : public ::testing::Test {
 protected:
  bool requested_ = false;
  std::vector<const Frame*> encoded_;
  std::optional<std::string> encode_result_ = "jpeg-bytes";
  std::vector<std::string> published_;
  bool publish_result_ = true;
  PreviewLease lease_{[](bool) {}};
  PreviewPublisher publisher_{[this] { return requested_; },
                              [this](const Frame& frame) {
                                encoded_.push_back(&frame);
                                return encode_result_;
                              },
                              [this](const std::string& bytes) {
                                published_.push_back(bytes);
                                return publish_result_;
                              },
                              lease_};
};

using OnFrameTest = PreviewPublisherTest;

TEST_F(OnFrameTest, EncodesNothingWithNoRequestAndTheCameraClosed) {
  publisher_.OnFrame(kFrame);

  EXPECT_FALSE(lease_.IsStreaming());
  EXPECT_THAT(encoded_, IsEmpty());
  EXPECT_THAT(published_, IsEmpty());
}

TEST_F(OnFrameTest, GrantsAndPublishesOnceTheCameraIsOpenAndItIsRequested) {
  lease_.CameraOpened();
  requested_ = true;

  publisher_.OnFrame(kFrame);

  EXPECT_TRUE(lease_.IsStreaming());
  EXPECT_THAT(encoded_, ElementsAre(&kFrame));
  EXPECT_THAT(published_, ElementsAre("jpeg-bytes"));
}

TEST_F(OnFrameTest, KeepsEncodingEachFrameWhileAlreadyStreaming) {
  lease_.CameraOpened();
  requested_ = true;
  publisher_.OnFrame(kFrame);

  publisher_.OnFrame(kFrame);

  EXPECT_TRUE(lease_.IsStreaming());
  EXPECT_THAT(encoded_, ElementsAre(&kFrame, &kFrame));
}

TEST_F(OnFrameTest, EncodesNothingWhenTheCameraIsClosedEvenIfRequested) {
  requested_ = true;

  publisher_.OnFrame(kFrame);

  EXPECT_FALSE(lease_.IsStreaming());
  EXPECT_THAT(encoded_, IsEmpty());
}

TEST_F(OnFrameTest, StopsOnceTheRequestGoesAway) {
  lease_.CameraOpened();
  requested_ = true;
  publisher_.OnFrame(kFrame);
  requested_ = false;

  publisher_.OnFrame(kFrame);

  EXPECT_FALSE(lease_.IsStreaming());
  EXPECT_THAT(encoded_, ElementsAre(&kFrame));
}

TEST_F(OnFrameTest, AFailedEncodePublishesNothing) {
  lease_.CameraOpened();
  requested_ = true;
  encode_result_ = std::nullopt;

  publisher_.OnFrame(kFrame);

  EXPECT_TRUE(lease_.IsStreaming());
  EXPECT_THAT(published_, IsEmpty());
}

}  // namespace
}  // namespace facepalm
