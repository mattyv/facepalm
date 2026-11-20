#include "src/camera_controller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::StrictMock;

class MockCamera : public Camera {
 public:
  MOCK_METHOD(void, Open, (std::uint64_t attempt), (override));
  MOCK_METHOD(void, Close, (), (override));
};

class MockClients : public Clients {
 public:
  MOCK_METHOD(void, Deliver, (const ClientId&, const Reaction&), (override));
};

using State = CameraController::State;

// Brings the camera to open with consent and one subscriber, "a".
void OpenWithClient(CameraController& controller) {
  controller.GrantConsent();
  controller.Subscribe("a");
  controller.OnOpened();
  ASSERT_EQ(controller.CurrentState(), State::kOpen);
}

// Returns the ticket for a frame captured now, failing the test when the
// controller drops the frame.
std::uint64_t CaptureTicket(CameraController& controller) {
  const std::optional<std::uint64_t> ticket =
      controller.OnFrameCaptured(controller.CurrentAttempt());
  if (!ticket.has_value()) {
    ADD_FAILURE() << "the frame was dropped";
    return 0;
  }
  return *ticket;
}

// Each suite below stands for one property in specs/CameraLifecycle.tla, and
// is named for it.
class CameraControllerTest : public testing::Test {
 protected:
  NiceMock<MockCamera> camera_;
  StrictMock<MockClients> clients_;
};

class OpenOnlyWithConsentTest : public CameraControllerTest {};

TEST_F(OpenOnlyWithConsentTest, NeverOpensForASubscriberWithoutConsent) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  EXPECT_CALL(camera_, Open(_)).Times(0);
  controller.Subscribe("a");
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
}

TEST_F(OpenOnlyWithConsentTest, RevokingClosesAnOpenCamera) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  EXPECT_CALL(camera_, Close());
  controller.RevokeConsent();
  EXPECT_EQ(controller.CurrentState(), State::kClosing);
}

TEST_F(OpenOnlyWithConsentTest, RevokingWhileOpeningClosesAndIgnoresALateOpen) {
  CameraController controller(camera_, clients_, /*consent=*/true);
  controller.Subscribe("a");
  ASSERT_EQ(controller.CurrentState(), State::kOpening);
  EXPECT_CALL(camera_, Close());
  controller.RevokeConsent();
  controller.OnOpened();
  EXPECT_EQ(controller.CurrentState(), State::kClosing);
}

TEST_F(OpenOnlyWithConsentTest, RevokingTwiceClosesOnce) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  EXPECT_CALL(camera_, Close()).Times(1);
  controller.RevokeConsent();
  controller.RevokeConsent();
}

TEST_F(OpenOnlyWithConsentTest, RevokingWithTheCameraClosedClosesNothing) {
  CameraController controller(camera_, clients_, /*consent=*/true);
  EXPECT_CALL(camera_, Close()).Times(0);
  controller.RevokeConsent();
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
}

class OpenOnlyWhenSubscribedTest : public CameraControllerTest {};

TEST_F(OpenOnlyWhenSubscribedTest, NeverOpensWithConsentAlone) {
  EXPECT_CALL(camera_, Open(_)).Times(0);
  CameraController controller(camera_, clients_, /*consent=*/true);
  controller.GrantConsent();
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
}

TEST_F(OpenOnlyWhenSubscribedTest, TheLastClientLeavingCloses) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  controller.Subscribe("b");
  EXPECT_CALL(camera_, Close()).Times(0);
  controller.Leave("a");
  EXPECT_EQ(controller.CurrentState(), State::kOpen);
  testing::Mock::VerifyAndClearExpectations(&camera_);
  EXPECT_CALL(camera_, Close());
  controller.Leave("b");
  EXPECT_EQ(controller.CurrentState(), State::kClosing);
}

TEST_F(OpenOnlyWhenSubscribedTest, LeavingWhenNotSubscribedChangesNothing) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  EXPECT_CALL(camera_, Close()).Times(0);
  controller.Leave("stranger");
  EXPECT_EQ(controller.CurrentState(), State::kOpen);
}

TEST_F(OpenOnlyWhenSubscribedTest,
       LastLeaveInvalidatesTicketsBeforeAReconnect) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  const std::uint64_t ticket = CaptureTicket(controller);
  controller.Leave("a");
  controller.Subscribe("b");
  controller.OnClosed();
  controller.OnOpened();
  EXPECT_CALL(clients_, Deliver(_, _)).Times(0);
  controller.OnReaction(ticket, "nod");
}

class NoEventWithoutConsentTest : public CameraControllerTest {};

TEST_F(NoEventWithoutConsentTest, RevokingDiscardsAFrameInFlight) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  const std::uint64_t ticket = CaptureTicket(controller);
  controller.RevokeConsent();
  controller.GrantConsent();
  EXPECT_CALL(clients_, Deliver(_, _)).Times(0);
  controller.OnReaction(ticket, "nod");
}

TEST_F(NoEventWithoutConsentTest, DropsAReactionWhileConsentIsRevoked) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  const std::uint64_t ticket = CaptureTicket(controller);
  controller.RevokeConsent();
  EXPECT_CALL(clients_, Deliver(_, _)).Times(0);
  controller.OnReaction(ticket, "nod");
}

TEST_F(NoEventWithoutConsentTest, DropsAFrameCapturedWhenNotOpen) {
  CameraController controller(camera_, clients_, /*consent=*/true);
  EXPECT_FALSE(
      controller.OnFrameCaptured(controller.CurrentAttempt()).has_value());
}

// specs/CameraLifecycle.tla's NoStaleEvent, with GuardTicketChecksCapture
// (#40): a frame stamped with an attempt that has ended gets no ticket,
// however late it arrives.
class NoStaleEventTest : public CameraControllerTest {};

TEST_F(NoStaleEventTest, AFrameFromBeforeARevokeAndReopenGetsNoTicket) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  const std::uint64_t old_attempt = controller.CurrentAttempt();
  controller.RevokeConsent();
  controller.OnClosed();
  controller.GrantConsent();
  controller.OnOpened();
  ASSERT_EQ(controller.CurrentState(), State::kOpen);

  EXPECT_FALSE(controller.OnFrameCaptured(old_attempt).has_value());
  EXPECT_TRUE(
      controller.OnFrameCaptured(controller.CurrentAttempt()).has_value());
}

TEST_F(NoStaleEventTest, EachOpenIsANewAttemptAndClosingEndsIt) {
  CameraController controller(camera_, clients_, /*consent=*/true);
  std::vector<std::uint64_t> opened;
  ON_CALL(camera_, Open(_)).WillByDefault([&opened](std::uint64_t attempt) {
    opened.push_back(attempt);
  });
  controller.Subscribe("a");
  controller.Leave("a");
  const std::uint64_t after_close = controller.CurrentAttempt();
  controller.OnClosed();
  controller.Subscribe("a");

  ASSERT_EQ(opened.size(), 2U);
  EXPECT_NE(opened[0], opened[1]);
  EXPECT_NE(after_close, opened[0]);
  EXPECT_EQ(controller.CurrentAttempt(), opened[1]);
}

TEST_F(NoStaleEventTest, LosingAnOpenCameraEndsItsAttempt) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  const std::uint64_t attempt = controller.CurrentAttempt();

  controller.OnDeviceLost();

  EXPECT_NE(controller.CurrentAttempt(), attempt);
}

class NoEventToStrangerTest : public CameraControllerTest {};

TEST_F(NoEventToStrangerTest, DeliversToEachSubscribedClientOnly) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  controller.Subscribe("b");
  controller.Subscribe("c");
  const std::uint64_t ticket = CaptureTicket(controller);
  controller.Leave("b");
  EXPECT_CALL(clients_, Deliver("a", "nod"));
  EXPECT_CALL(clients_, Deliver("c", "nod"));
  controller.OnReaction(ticket, "nod");
}

class DeadHelperHoldsNoCameraTest : public CameraControllerTest {};

TEST_F(DeadHelperHoldsNoCameraTest, ARestartedControllerStartsClosed) {
  EXPECT_CALL(camera_, Open(_)).Times(0);
  const CameraController controller(camera_, clients_, /*consent=*/true);
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
}

class WantedOpensTest : public CameraControllerTest {};

TEST_F(WantedOpensTest, OpensOnceConsentAndASubscriberHold) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  controller.Subscribe("a");
  EXPECT_CALL(camera_, Open(_)).Times(1);
  controller.GrantConsent();
  controller.Subscribe("b");
  EXPECT_EQ(controller.CurrentState(), State::kOpening);
  controller.OnOpened();
  EXPECT_EQ(controller.CurrentState(), State::kOpen);
}

TEST_F(WantedOpensTest, ReopensWhenTheDeviceComesBack) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  controller.OnDeviceLost();
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
  EXPECT_CALL(camera_, Open(_));
  controller.OnDeviceBack();
  EXPECT_EQ(controller.CurrentState(), State::kOpening);
}

TEST_F(WantedOpensTest, WaitsForTheDeviceAfterTheOsRefusesIt) {
  CameraController controller(camera_, clients_, /*consent=*/true);
  {
    InSequence order;
    EXPECT_CALL(camera_, Open(_));
    EXPECT_CALL(camera_, Open(_));
  }
  controller.Subscribe("a");
  controller.OnOpenFailed();
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
  controller.Subscribe("b");
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
  controller.OnDeviceBack();
  EXPECT_EQ(controller.CurrentState(), State::kOpening);
}

TEST_F(WantedOpensTest, ReopensAfterClosingWhenWantedAgain) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  controller.RevokeConsent();
  controller.GrantConsent();
  EXPECT_EQ(controller.CurrentState(), State::kClosing);
  EXPECT_CALL(camera_, Open(_));
  controller.OnClosed();
  EXPECT_EQ(controller.CurrentState(), State::kOpening);
}

TEST_F(WantedOpensTest, LosingTheDeviceWhileOpeningWaitsForTheFailure) {
  CameraController controller(camera_, clients_, /*consent=*/true);
  controller.Subscribe("a");
  controller.OnDeviceLost();
  EXPECT_EQ(controller.CurrentState(), State::kOpening);
  controller.OnOpenFailed();
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
}

class RevokingClosesTest : public CameraControllerTest {};

TEST_F(RevokingClosesTest, ClosedOnceTheCameraReportsIt) {
  CameraController controller(camera_, clients_, /*consent=*/false);
  OpenWithClient(controller);
  controller.RevokeConsent();
  EXPECT_CALL(camera_, Open(_)).Times(0);
  controller.OnClosed();
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
}

class LateReportsTest : public CameraControllerTest {};

TEST_F(LateReportsTest, ReportsOutOfTurnChangeNothing) {
  CameraController controller(camera_, clients_, /*consent=*/true);
  EXPECT_CALL(camera_, Open(_)).Times(0);
  controller.OnOpened();
  controller.OnOpenFailed();
  controller.OnClosed();
  controller.OnDeviceBack();
  EXPECT_EQ(controller.CurrentState(), State::kClosed);
}

}  // namespace
}  // namespace facepalm
