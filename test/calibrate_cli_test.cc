#include "src/calibrate_cli.h"

#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Return;

class MockCalibrateClientIo : public CalibrateClientIo {
 public:
  MOCK_METHOD(bool, Connect, (const std::string&), (override));
  MOCK_METHOD(void, WriteLine, (std::string_view), (override));
  MOCK_METHOD(std::optional<std::string>, ReadLine, (), (override));
};

class RunCalibrateCliTest : public ::testing::Test {
 protected:
  testing::NiceMock<MockCalibrateClientIo> io_;
  std::ostringstream out_;
  std::ostringstream err_;
};

TEST_F(RunCalibrateCliTest, SendsHelloThenTheJoinedCommand) {
  std::vector<std::string> sent;
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  ON_CALL(io_, WriteLine).WillByDefault([&](std::string_view line) {
    sent.emplace_back(line);
  });
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(
          R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"));

  const int code =
      RunCalibrateCli({"/tmp/service.sock", "start", "1"}, io_, out_, err_);

  EXPECT_THAT(code, Eq(0));
  EXPECT_THAT(sent, ElementsAre("hello calibrate-cli", "calibrate start 1"));
  EXPECT_THAT(
      out_.str(),
      Eq(R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"
         "\n"));
  EXPECT_THAT(err_.str(), Eq(""));
}

TEST_F(RunCalibrateCliTest, JoinsNextAndCancelWithNoExtraWords) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  std::vector<std::string> sent;
  ON_CALL(io_, WriteLine).WillByDefault([&](std::string_view line) {
    sent.emplace_back(line);
  });
  ON_CALL(io_, ReadLine).WillByDefault(Return(R"({"ok":true})"));

  RunCalibrateCli({"/tmp/service.sock", "next"}, io_, out_, err_);

  EXPECT_THAT(sent, ElementsAre("hello calibrate-cli", "calibrate next"));
}

TEST_F(RunCalibrateCliTest, RefusesTooFewArguments) {
  EXPECT_CALL(io_, Connect).Times(0);

  const int code = RunCalibrateCli({"/tmp/service.sock"}, io_, out_, err_);

  EXPECT_THAT(code, Eq(2));
  EXPECT_THAT(err_.str(), HasSubstr("usage"));
}

TEST_F(RunCalibrateCliTest, ReportsAConnectionFailure) {
  EXPECT_CALL(io_, Connect).WillOnce(Return(false));

  const int code =
      RunCalibrateCli({"/tmp/service.sock", "cancel"}, io_, out_, err_);

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr("/tmp/service.sock"));
}

TEST_F(RunCalibrateCliTest, ReportsAHelloRefused) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"error":"client not accepted"})"));

  const int code =
      RunCalibrateCli({"/tmp/service.sock", "cancel"}, io_, out_, err_);

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr("client not accepted"));
}

TEST_F(RunCalibrateCliTest, ReportsTheConnectionClosingBeforeHello) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(std::nullopt));

  const int code =
      RunCalibrateCli({"/tmp/service.sock", "cancel"}, io_, out_, err_);

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr("refused"));
}

TEST_F(RunCalibrateCliTest, ReportsTheConnectionClosingBeforeTheReply) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(std::nullopt));

  const int code =
      RunCalibrateCli({"/tmp/service.sock", "cancel"}, io_, out_, err_);

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr("closed"));
}

TEST_F(RunCalibrateCliTest, AnErrorReplyExitsNonZero) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(R"({"error":"no calibration session is running"})"));

  const int code =
      RunCalibrateCli({"/tmp/service.sock", "next"}, io_, out_, err_);

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(out_.str(), HasSubstr("no calibration session is running"));
}

}  // namespace
}  // namespace facepalm
