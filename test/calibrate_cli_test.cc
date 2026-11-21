#include "src/calibrate_cli.h"

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

constexpr auto kSocketPath = "/tmp/service.sock";

class MockCalibrateClientIo : public CalibrateClientIo {
 public:
  MOCK_METHOD(bool, Connect, (const std::string&), (override));
  MOCK_METHOD(void, WriteLine, (std::string_view), (override));
  MOCK_METHOD(std::optional<std::string>, ReadLine, (), (override));
};

class RunCalibrateCliTest : public ::testing::Test {
 protected:
  int Run(const std::vector<std::string>& arguments,
          const std::string& token = "") {
    return RunCalibrateCli(arguments, kSocketPath, token, io_, out_, err_);
  }

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

  const int code = Run({"start", "1"});

  EXPECT_THAT(code, Eq(0));
  EXPECT_THAT(sent, ElementsAre("hello calibrate", "calibrate start 1"));
  EXPECT_THAT(
      out_.str(),
      Eq(R"({"ok":true,"calibration":{"state":"running","index":0,"total":6,"step":"resting"}})"
         "\n"));
  EXPECT_THAT(err_.str(), Eq(""));
}

TEST_F(RunCalibrateCliTest, SendsTheApprovedTokenWhenGivenOne) {
  std::vector<std::string> sent;
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  ON_CALL(io_, WriteLine).WillByDefault([&](std::string_view line) {
    sent.emplace_back(line);
  });
  ON_CALL(io_, ReadLine).WillByDefault(Return(R"({"ok":true})"));

  Run({"cancel"}, std::string(64, 'a'));

  EXPECT_THAT(sent, ElementsAre("hello calibrate " + std::string(64, 'a'),
                                "calibrate cancel"));
}

TEST_F(RunCalibrateCliTest, JoinsNextAndCancelWithNoExtraWords) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  std::vector<std::string> sent;
  ON_CALL(io_, WriteLine).WillByDefault([&](std::string_view line) {
    sent.emplace_back(line);
  });
  ON_CALL(io_, ReadLine).WillByDefault(Return(R"({"ok":true})"));

  Run({"next"});

  EXPECT_THAT(sent, ElementsAre("hello calibrate", "calibrate next"));
}

TEST_F(RunCalibrateCliTest, RefusesNoArguments) {
  EXPECT_CALL(io_, Connect).Times(0);

  const int code = Run({});

  EXPECT_THAT(code, Eq(2));
  EXPECT_THAT(err_.str(), HasSubstr("usage"));
}

TEST_F(RunCalibrateCliTest, ReportsAConnectionFailure) {
  EXPECT_CALL(io_, Connect).WillOnce(Return(false));

  const int code = Run({"cancel"});

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr(kSocketPath));
}

TEST_F(RunCalibrateCliTest, ReportsAHelloRefused) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"error":"client not approved"})"));

  const int code = Run({"cancel"});

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr("client not approved"));
}

TEST_F(RunCalibrateCliTest, ReportsTheConnectionClosingBeforeHello) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine).WillOnce(Return(std::nullopt));

  const int code = Run({"cancel"});

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr("refused"));
}

TEST_F(RunCalibrateCliTest, ReportsTheConnectionClosingBeforeTheReply) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(std::nullopt));

  const int code = Run({"cancel"});

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(err_.str(), HasSubstr("closed"));
}

TEST_F(RunCalibrateCliTest, AnErrorReplyExitsNonZero) {
  ON_CALL(io_, Connect).WillByDefault(Return(true));
  EXPECT_CALL(io_, ReadLine)
      .WillOnce(Return(R"({"ok":true})"))
      .WillOnce(Return(R"({"error":"no calibration session is running"})"));

  const int code = Run({"next"});

  EXPECT_THAT(code, Eq(1));
  EXPECT_THAT(out_.str(), HasSubstr("no calibration session is running"));
}

}  // namespace
}  // namespace facepalm
