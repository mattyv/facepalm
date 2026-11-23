#include "src/calibration_flow.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/calibration.h"
#include "src/protocol.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Return;

constexpr auto kSocketPath = "/tmp/service.sock";

class MockCalibrateClientIo : public CalibrateClientIo {
 public:
  MOCK_METHOD(bool, Connect, (const std::string&), (override));
  MOCK_METHOD(void, WriteLine, (std::string_view), (override));
  MOCK_METHOD(std::optional<std::string>, ReadLine, (), (override));
};

class CalibrationFlowTest : public ::testing::Test {
 protected:
  // `wait` defaults to one that counts its own calls without really
  // sleeping, so a test exercising Start's camera-wait retry (#128) runs
  // at once; a test wanting the real duration passed instead gives its
  // own.
  CalibrationFlow Make(const std::string& token = "") {
    return {kSocketPath, token, io_,
            [this](std::chrono::milliseconds wait) { waits_.push_back(wait); }};
  }

  std::vector<std::string> Sent() const { return sent_; }
  const std::vector<std::chrono::milliseconds>& Waits() const { return waits_; }

  // Records every WriteLine call and, by default, lets Connect succeed.
  void RecordWrites() {
    ON_CALL(io_, Connect).WillByDefault(Return(true));
    ON_CALL(io_, WriteLine).WillByDefault([this](std::string_view line) {
      sent_.emplace_back(line);
    });
  }

  testing::NiceMock<MockCalibrateClientIo> io_;

 private:
  std::vector<std::string> sent_;
  std::vector<std::chrono::milliseconds> waits_;
};

// -- Start -------------------------------------------------------------

TEST_F(CalibrationFlowTest, StartSendsHelloThenStartAndReachesPrompting) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"));
  CalibrationFlow flow = Make();

  flow.Start(1);

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1"));
  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kPrompting));
  EXPECT_TRUE(view.resting_step);
  EXPECT_THAT(view.index, Eq(0));
  EXPECT_THAT(view.total, Eq(6));
}

TEST_F(CalibrationFlowTest, StartSendsTheApprovedTokenWhenGivenOne) {
  RecordWrites();
  ON_CALL(io_, ReadLine).WillByDefault(Return(R"({"ok":true})"));
  CalibrationFlow flow = Make(std::string(64, 'a'));

  flow.Start(2);

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate " + std::string(64, 'a'),
                                  "calibrate start 2"));
}

TEST_F(CalibrationFlowTest, StartShowsAScreenStepWithItsPoint) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":2,"total":6,"step":"screen","screen":1,"point":"top_right"}})"));
  CalibrationFlow flow = Make();

  flow.Start(2);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kPrompting));
  EXPECT_FALSE(view.resting_step);
  EXPECT_THAT(view.screen, Eq(1));
  EXPECT_THAT(view.point, Eq(CalibrationPoint::kTopRight));
  EXPECT_THAT(view.index, Eq(2));
  EXPECT_THAT(view.total, Eq(6));
}

TEST_F(CalibrationFlowTest, StartShowsTheGainsStep) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":1,"total":7,"step":"gains"}})"));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kPrompting));
  EXPECT_FALSE(view.resting_step);
  EXPECT_TRUE(view.gains_step);
  EXPECT_THAT(view.index, Eq(1));
}

TEST_F(CalibrationFlowTest, StartReachesFailedOnAConnectFailure) {
  EXPECT_CALL(io_, Connect).WillOnce(Return(false));
  EXPECT_CALL(io_, WriteLine).Times(0);
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, HasSubstr(kSocketPath));
}

TEST_F(CalibrationFlowTest, StartReachesFailedOnAHelloRefused) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"error":"client not approved"})"));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, Eq("client not approved"));
  // No "calibrate start" is ever sent once hello is refused.
  EXPECT_THAT(Sent(), ElementsAre("hello calibrate"));
}

// #178: StringField undoes every escape protocol::Quoted (src/protocol.cc)
// can write, round-tripped here through the real ErrorJson rather than a
// literal, so a refusal carrying a quote, a backslash, a newline and a
// control character comes back exactly as the daemon meant it, not
// truncated at the first escaped quote.
TEST_F(CalibrationFlowTest,
       StartReachesFailedOnAHelloRefusalWithEscapedCharacters) {
  RecordWrites();
  const std::string message = "disk full: \"C:\\data\"\n\x01 -- try again";
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(protocol::ErrorJson(message)));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, Eq(message));
}

// A lone backslash right where the line ends -- never something Quoted
// writes, but something a daemon crash mid-write could still leave on the
// wire -- is an unterminated string, the same as running off the end with
// no backslash at all (NextTreatsATruncatedReplyAsFailure below).
TEST_F(CalibrationFlowTest, StartTreatsATrailingBackslashAsFailure) {
  RecordWrites();
  const std::string truncated_after_backslash = R"({"error":"disk\)";
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(truncated_after_backslash));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, Eq(truncated_after_backslash));
}

// A \u escape truncated before its 4 hex digits arrive -- with the whole
// line ending right there, nothing to even misread as a closing quote --
// is also an unterminated string, not a crash reading past the buffer.
TEST_F(CalibrationFlowTest, StartTreatsATruncatedUnicodeEscapeAsFailure) {
  RecordWrites();
  const std::string truncated_in_escape = R"({"error":"disk \u12)";
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(truncated_in_escape));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, Eq(truncated_in_escape));
}

// \u followed by something other than 4 hex digits is not an escape
// Quoted ever writes, so it is refused rather than read as whatever
// from_chars happened to stop at.
TEST_F(CalibrationFlowTest, StartTreatsANonHexUnicodeEscapeAsFailure) {
  RecordWrites();
  const std::string bad_hex = R"({"error":"disk \u00zz full"})";
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(bad_hex));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, Eq(bad_hex));
}

// \u00xx is the only form Quoted ever writes, always for a control
// character below 0x20; a \u naming anything else is refused rather than
// silently decoded, since nothing this client reads could have written
// it.
TEST_F(CalibrationFlowTest,
       StartTreatsAUnicodeEscapeAboveControlRangeAsFailure) {
  RecordWrites();
  const std::string out_of_range = R"({"error":"disk \u0041 full"})";
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(out_of_range));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, Eq(out_of_range));
}

// Quoted never writes a bare \t -- a control character always goes
// through \u00xx instead (see protocol.cc's Quoted) -- so this is refused
// like any other escape it does not emit.
TEST_F(CalibrationFlowTest, StartTreatsAnUnknownEscapeAsFailure) {
  RecordWrites();
  const std::string unknown_escape = R"({"error":"disk \t full"})";
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(unknown_escape));
  CalibrationFlow flow = Make();

  flow.Start(1);

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(view.error, Eq(unknown_escape));
}

TEST_F(CalibrationFlowTest,
       StartReachesFailedWhenTheConnectionClosesBeforeHello) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(std::nullopt));
  CalibrationFlow flow = Make();

  flow.Start(1);

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(flow.CurrentView().error, HasSubstr("closed"));
}

TEST_F(CalibrationFlowTest, StartReachesFailedOnAnErrorReplyToStart) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(R"({"error":"calibration is not available"})"));
  CalibrationFlow flow = Make();

  flow.Start(1);

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(flow.CurrentView().error, Eq("calibration is not available"));
}

TEST_F(CalibrationFlowTest,
       StartReachesFailedWhenTheConnectionClosesBeforeItsOwnReply) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(std::nullopt));
  CalibrationFlow flow = Make();

  flow.Start(1);

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(flow.CurrentView().error, HasSubstr("closed"));
}

// -- Start's camera-wait retry (CalibrationMenu.tla's OpenWaitsForCamera, --
// -- RetrySucceeds, RetryWaits, RetryGivesUp; #128's first finding) ------

TEST_F(CalibrationFlowTest,
       StartRetriesWhenRefusedOnlyForTheCameraThenReachesPrompting) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(R"({"error":"the camera is not available"})"))
      .WillOnce(Return(R"({"error":"the camera is not available"})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"));
  CalibrationFlow flow = Make();

  flow.Start(1);

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1",
                                  "calibrate start 1", "calibrate start 1"));
  EXPECT_THAT(Waits(), ElementsAre(CalibrationFlow::kCameraWaitInterval,
                                   CalibrationFlow::kCameraWaitInterval));
  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kPrompting));
}

TEST_F(CalibrationFlowTest,
       StartGivesUpAfterMaxCameraWaitsWithTheCameraStillDown) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillRepeatedly(Return(R"({"error":"the camera is not available"})"));
  CalibrationFlow flow = Make();

  flow.Start(1);

  // One hello, then one "calibrate start" per attempt (kMaxCameraWaits
  // retries plus the first try).
  EXPECT_THAT(
      Sent().size(),
      Eq(1U + static_cast<std::size_t>(CalibrationFlow::kMaxCameraWaits + 1)));
  EXPECT_THAT(Waits().size(),
              Eq(static_cast<std::size_t>(CalibrationFlow::kMaxCameraWaits)));
  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(flow.CurrentView().error, Eq("the camera is not available"));
}

// The real default (std::this_thread::sleep_for), used outside a test
// double, actually sleeps between retries rather than only being wired up
// to do so.
TEST_F(CalibrationFlowTest, StartsDefaultWaitReallySleeps) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(R"({"error":"the camera is not available"})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"));
  CalibrationFlow flow{kSocketPath, "", io_};

  flow.Start(1);

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kPrompting));
}

TEST_F(CalibrationFlowTest, StartDoesNotRetryARefusalUnrelatedToTheCamera) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(
          Return(R"({"error":"a calibration session is already running"})"));
  CalibrationFlow flow = Make();

  flow.Start(1);

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1"));
  EXPECT_THAT(Waits(), IsEmpty());
  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(flow.CurrentView().error,
              Eq("a calibration session is already running"));
}

// -- Next ----------------------------------------------------------------

TEST_F(CalibrationFlowTest, NextTransitionsToPromptingOnARunningReply) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":1,"total":6,"step":"screen","screen":0,"point":"centre"}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1",
                                  "calibrate next"));
  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kPrompting));
  EXPECT_THAT(view.index, Eq(1));
  EXPECT_THAT(view.point, Eq(CalibrationPoint::kCentre));
}

TEST_F(CalibrationFlowTest, NextTransitionsToDoneOnADoneReply) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":5,"total":6,"step":"screen","screen":0,"point":"bottom_right"}})"))
      .WillOnce(Return(R"({"ok":true,"calibration":{"state":"done"}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kDone));
  EXPECT_THAT(flow.CurrentView().error, IsEmpty());
}

// #135, #137: the wizard's own result line ("Calibrated N screens.") is
// built from this field.
TEST_F(CalibrationFlowTest, NextTransitionsToDoneWithTheScreensCalibrated) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":5,"total":6,"step":"screen","screen":0,"point":"bottom_right"}})"))
      .WillOnce(
          Return(R"({"ok":true,"calibration":{"state":"done","screens":2}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kDone));
  EXPECT_THAT(flow.CurrentView().screens, Eq(2));
}

// CalibrationMenu.tla's NextRefused (#177): a refused "next" stays
// "prompting" on the same step, with the daemon's own hint, and sends no
// cleanup cancel -- the daemon never gave up the session over a refusal
// (src/calibration.cc's Next only ResetStep()s it), so neither does this
// flow.
TEST_F(CalibrationFlowTest, NextStaysPromptingWithTheDaemonsHintOnARefusal) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,)"
          R"("step":"resting","refused":"no pose captured yet for this step"}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1",
                                  "calibrate next"));
  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kPrompting));
  EXPECT_TRUE(view.resting_step);
  EXPECT_THAT(view.hint, Eq("no pose captured yet for this step"));
}

// A save-error hint with a quote, a backslash, punctuation and a
// non-ASCII character round-trips through protocol::CalibrationStatusJson's
// own Quoted() and back (#177, #178), rather than breaking the reply's
// shape.
TEST_F(CalibrationFlowTest, NextCarriesARefusalHintWithPunctuation) {
  RecordWrites();
  const CalibrationController::Status status{
      .running = true,
      .index = 0,
      .total = 6,
      .resting_step = true,
      .refusal = R"(the disk "home" at C:\\ is 90% full — try again)"};
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(protocol::CalibrationStatusJson(status)));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kPrompting));
  EXPECT_THAT(flow.CurrentView().hint,
              Eq(R"(the disk "home" at C:\\ is 90% full — try again)"));
}

// A hang-up, unlike a refusal, means the connection -- and whatever
// session the daemon had -- is already gone: there is nothing left to
// send a cancel to, so Next does not try.
TEST_F(CalibrationFlowTest, NextTransitionsToFailedOnAHangUpWithoutACancel) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(std::nullopt));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1",
                                  "calibrate next"));
  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(flow.CurrentView().error, HasSubstr("closed"));
}

TEST_F(CalibrationFlowTest, NextTransitionsToFailedOnAnUnexpectedIdleReply) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(R"({"ok":true,"calibration":{"state":"idle"}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
}

TEST_F(CalibrationFlowTest, NextTreatsATruncatedReplyAsFailure) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      // No closing quote after "running": the daemon's own connection, or
      // the message itself, ended mid-reply.
      .WillOnce(Return(R"({"calibration":{"state":"running)"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
}

TEST_F(CalibrationFlowTest, NextDefaultsAMissingNumericFieldToZero) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      // "index" is missing entirely.
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","total":6,"step":"resting"}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  const CalibrationFlow::View& view = flow.CurrentView();
  EXPECT_THAT(view.state, Eq(CalibrationFlow::State::kPrompting));
  EXPECT_THAT(view.index, Eq(0));
  EXPECT_THAT(view.total, Eq(6));
}

TEST_F(CalibrationFlowTest, NextDefaultsAMalformedNumericFieldToZero) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      // "index" is not a number.
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":"x","total":6,"step":"resting"}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Next();

  EXPECT_THAT(flow.CurrentView().index, Eq(0));
}

TEST_F(CalibrationFlowTest, NextIsANoOpBeforeStarting) {
  CalibrationFlow flow = Make();
  EXPECT_CALL(io_, WriteLine).Times(0);

  flow.Next();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kClosed));
}

TEST_F(CalibrationFlowTest, NextIsANoOpOnceDone) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(R"({"ok":true,"calibration":{"state":"done"}})"));
  CalibrationFlow flow = Make();
  flow.Start(1);
  ASSERT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kDone));

  EXPECT_CALL(io_, WriteLine(std::string_view("calibrate next"))).Times(0);
  flow.Next();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kDone));
}

// -- Cancel ----------------------------------------------------------------

TEST_F(CalibrationFlowTest, CancelSendsCancelAndReturnsToClosedOnOk) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(R"({"ok":true})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Cancel();

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1",
                                  "calibrate cancel"));
  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kClosed));
}

TEST_F(CalibrationFlowTest, CancelReachesFailedOnAnErrorReply) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(R"({"error":"no calibration session is running"})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Cancel();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
  EXPECT_THAT(flow.CurrentView().error,
              Eq("no calibration session is running"));
}

TEST_F(CalibrationFlowTest, CancelReachesFailedOnAHangUp) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(std::nullopt));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.Cancel();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
}

TEST_F(CalibrationFlowTest, CancelIsANoOpBeforeStarting) {
  CalibrationFlow flow = Make();
  EXPECT_CALL(io_, WriteLine).Times(0);

  flow.Cancel();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kClosed));
}

TEST_F(CalibrationFlowTest, CancelIsANoOpOnceFailed) {
  EXPECT_CALL(io_, Connect).WillOnce(Return(false));
  CalibrationFlow flow = Make();
  flow.Start(1);
  ASSERT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));

  EXPECT_CALL(io_, WriteLine).Times(0);
  flow.Cancel();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kFailed));
}

// -- WindowClosed (CalibrationMenu.tla's WindowClosed / NoOrphanOnClose) --

TEST_F(CalibrationFlowTest, WindowClosedSendsCancelWhilePrompting) {
  RecordWrites();
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"))
      .WillOnce(Return(R"({"ok":true})"));
  CalibrationFlow flow = Make();
  flow.Start(1);

  flow.WindowClosed();

  EXPECT_THAT(Sent(), ElementsAre("hello calibrate", "calibrate start 1",
                                  "calibrate cancel"));
  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kClosed));
}

TEST_F(CalibrationFlowTest, WindowClosedIsANoOpOutsidePrompting) {
  CalibrationFlow flow = Make();
  EXPECT_CALL(io_, WriteLine).Times(0);

  flow.WindowClosed();

  EXPECT_THAT(flow.CurrentView().state, Eq(CalibrationFlow::State::kClosed));
}

}  // namespace
}  // namespace facepalm
