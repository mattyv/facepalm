#include "src/calibrate_client_token.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/client_approvals.h"

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Return;

constexpr std::string_view kDirectory = "/home/p/clients";

class MockApprovalsIo : public ApprovalsIo {
 public:
  MOCK_METHOD(std::optional<std::vector<std::string>>, List,
              (const std::string&), (override));
  MOCK_METHOD(bool, MakePrivateDirectory, (const std::string&), (override));
  MOCK_METHOD(bool, CreatePrivate, (const std::string&, std::string_view),
              (override));
  MOCK_METHOD(bool, Remove, (const std::string&), (override));
  MOCK_METHOD(std::optional<std::string>, Random, (std::size_t), (override));
};

class ResolveCalibrateTokenTest : public ::testing::Test {
 protected:
  ResolveCalibrateTokenTest()
      : approvals_(std::string(kDirectory), approve_load_.AsStdFunction(),
                   io_) {}

  ::testing::MockFunction<std::optional<std::string>(const std::string&)>
      approve_load_;
  ::testing::MockFunction<std::optional<std::string>(const std::string&)>
      token_load_;
  MockApprovalsIo io_;
  ClientApprovals approvals_;
};

TEST_F(ResolveCalibrateTokenTest, ReturnsTheLoadedToken) {
  EXPECT_CALL(io_, MakePrivateDirectory).WillOnce(Return(true));
  EXPECT_CALL(approve_load_, Call).WillOnce(Return(std::nullopt));
  EXPECT_CALL(io_, Random(32)).WillOnce(Return(std::string(32, '\xa5')));
  EXPECT_CALL(io_, CreatePrivate).WillOnce(Return(true));
  EXPECT_CALL(token_load_, Call(Eq("/home/p/clients/calibrate")))
      .WillOnce(Return("the-token"));

  const CalibrateClientToken result =
      ResolveCalibrateToken(approvals_, token_load_.AsStdFunction());

  EXPECT_THAT(result.token, Eq("the-token"));
  EXPECT_THAT(result.error, IsEmpty());
}

TEST_F(ResolveCalibrateTokenTest, ReturnsAnEmptyTokenWhenTheLoadFails) {
  EXPECT_CALL(io_, MakePrivateDirectory).WillOnce(Return(true));
  EXPECT_CALL(approve_load_, Call).WillOnce(Return(std::nullopt));
  EXPECT_CALL(io_, Random(32)).WillOnce(Return(std::string(32, '\xa5')));
  EXPECT_CALL(io_, CreatePrivate).WillOnce(Return(true));
  EXPECT_CALL(token_load_, Call).WillOnce(Return(std::nullopt));

  const CalibrateClientToken result =
      ResolveCalibrateToken(approvals_, token_load_.AsStdFunction());

  EXPECT_THAT(result.token, Eq(""));
  EXPECT_THAT(result.error, IsEmpty());
}

TEST_F(ResolveCalibrateTokenTest, ReturnsTheErrorWhenApprovalFails) {
  EXPECT_CALL(io_, MakePrivateDirectory).WillOnce(Return(false));
  EXPECT_CALL(token_load_, Call).Times(0);

  const CalibrateClientToken result =
      ResolveCalibrateToken(approvals_, token_load_.AsStdFunction());

  EXPECT_THAT(result.token, Eq(std::nullopt));
  EXPECT_THAT(result.error, Not(IsEmpty()));
}

}  // namespace
}  // namespace facepalm
