#include "src/client_approvals.h"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// The ApprovedOnly and ApprovedAdmitted tests stand for those properties of
// specs/Service.tla.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

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

class ClientApprovalsTest : public ::testing::Test {
 protected:
  ClientApprovalsTest()
      : approvals_(std::string(kDirectory), load_.AsStdFunction(), io_) {}

  ::testing::MockFunction<std::optional<std::string>(const std::string&)> load_;
  MockApprovalsIo io_;
  ClientApprovals approvals_;
};

TEST(ClientNameTest, AcceptsLowercaseNamesOfUpTo64) {
  EXPECT_TRUE(ClientApprovals::IsClientName("mcp"));
  EXPECT_TRUE(ClientApprovals::IsClientName("ai-pair_2.0"));
  EXPECT_TRUE(ClientApprovals::IsClientName(std::string(64, 'z')));
}

TEST(ClientNameTest, RefusesAnythingElse) {
  for (const std::string& name :
       {std::string(), std::string(65, 'a'), std::string("."),
        std::string(".hidden"), std::string("a/b"), std::string("Upper"),
        std::string("a b"), std::string("a`"), std::string("a{")}) {
    EXPECT_FALSE(ClientApprovals::IsClientName(name)) << name;
  }
}

using ApprovedOnlyTest = ClientApprovalsTest;

TEST_F(ApprovedOnlyTest, ATokenNoClientHoldsIsNotApproved) {
  EXPECT_CALL(io_, List(Eq(std::string(kDirectory))))
      .WillOnce(
          Return(std::vector<std::string>{"mcp", "ai-pair", ".mcp.partial"}));
  EXPECT_CALL(load_, Call(Eq("/home/p/clients/mcp"))).WillOnce(Return("aaaa"));
  EXPECT_CALL(load_, Call(Eq("/home/p/clients/ai-pair")))
      .WillOnce(Return(std::nullopt));

  EXPECT_FALSE(approvals_.Approved("bbbb"));
}

TEST_F(ApprovedOnlyTest, NoTokenAndAnUnreadableDirectoryApproveNothing) {
  EXPECT_FALSE(approvals_.Approved(""));
  EXPECT_CALL(io_, List).WillOnce(Return(std::nullopt));
  EXPECT_FALSE(approvals_.Approved("aaaa"));
}

TEST_F(ApprovedOnlyTest, ATokenOfAnotherLengthIsNotApproved) {
  EXPECT_CALL(io_, List).WillOnce(Return(std::vector<std::string>{"mcp"}));
  EXPECT_CALL(load_, Call).WillOnce(Return("aaaa"));

  EXPECT_FALSE(approvals_.Approved("aaa"));
}

using ApprovedAdmittedTest = ClientApprovalsTest;

TEST_F(ApprovedAdmittedTest, ATokenAClientHoldsIsApproved) {
  EXPECT_CALL(io_, List).WillOnce(
      Return(std::vector<std::string>{"mcp", "ai-pair"}));
  EXPECT_CALL(load_, Call(Eq("/home/p/clients/mcp"))).WillOnce(Return("aaaa"));
  EXPECT_CALL(load_, Call(Eq("/home/p/clients/ai-pair")))
      .WillOnce(Return("bbbb"));

  EXPECT_TRUE(approvals_.Approved("bbbb"));
}

TEST_F(ClientApprovalsTest, ApprovingMakesANewPrivateRandomToken) {
  EXPECT_CALL(io_, MakePrivateDirectory(Eq(std::string(kDirectory))))
      .WillOnce(Return(true));
  EXPECT_CALL(load_, Call(Eq("/home/p/clients/mcp")))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(io_, Random(32)).WillOnce(Return(std::string(32, '\xa5')));
  std::string expected;
  for (int i = 0; i < 32; ++i) {
    expected += "a5";
  }
  EXPECT_CALL(io_,
              CreatePrivate(Eq("/home/p/clients/mcp"), Eq(expected + "\n")))
      .WillOnce(Return(true));
  std::string error;

  EXPECT_THAT(approvals_.Approve("mcp", error), Eq("/home/p/clients/mcp"));
}

TEST_F(ClientApprovalsTest, ApprovingAgainKeepsTheToken) {
  EXPECT_CALL(io_, MakePrivateDirectory).WillOnce(Return(true));
  EXPECT_CALL(load_, Call).WillOnce(Return(std::string(64, 'a')));
  EXPECT_CALL(io_, CreatePrivate).Times(0);
  std::string error;

  EXPECT_THAT(approvals_.Approve("mcp", error), Eq("/home/p/clients/mcp"));
}

TEST_F(ClientApprovalsTest, ApprovingSaysWhyItFailed) {
  std::string error;
  EXPECT_THAT(approvals_.Approve("Bad/Name", error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("a client name is 1 to 64 of a-z 0-9 . _ -"));

  EXPECT_CALL(io_, MakePrivateDirectory).WillOnce(Return(false));
  EXPECT_THAT(approvals_.Approve("mcp", error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("cannot make /home/p/clients"));

  EXPECT_CALL(io_, MakePrivateDirectory).WillRepeatedly(Return(true));
  EXPECT_CALL(load_, Call).WillRepeatedly(Return(std::nullopt));
  EXPECT_CALL(io_, Random).WillOnce(Return(std::nullopt));
  EXPECT_THAT(approvals_.Approve("mcp", error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("cannot make a random token"));

  EXPECT_CALL(io_, Random).WillOnce(Return(std::string(32, 'x')));
  EXPECT_CALL(io_, CreatePrivate).WillOnce(Return(false));
  EXPECT_THAT(approvals_.Approve("mcp", error), Eq(std::nullopt));
  EXPECT_THAT(error, Eq("cannot write /home/p/clients/mcp"));
}

TEST_F(ClientApprovalsTest, RevokingRemovesTheTokenFile) {
  EXPECT_FALSE(approvals_.Revoke(".."));
  EXPECT_CALL(io_, Remove(Eq("/home/p/clients/mcp"))).WillOnce(Return(true));
  EXPECT_TRUE(approvals_.Revoke("mcp"));
  EXPECT_CALL(io_, Remove).WillOnce(Return(false));
  EXPECT_FALSE(approvals_.Revoke("mcp"));
}

// The OS's calls, on a temporary directory the test removes.
class PosixApprovalsIoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::array<char, 32> path = {"/tmp/fp-approvals-XXXXXX"};
    ASSERT_NE(mkdtemp(path.data()), nullptr);
    root_ = path.data();
  }
  void TearDown() override { std::filesystem::remove_all(root_); }

  std::string root_;
  std::unique_ptr<ApprovalsIo> io_ = PosixApprovalsIo();
};

TEST_F(PosixApprovalsIoTest, MakesPrivateDirectoriesAndFiles) {
  const std::string clients = root_ + "/state/facepalm/clients";
  ASSERT_TRUE(io_->MakePrivateDirectory(clients));
  ASSERT_TRUE(io_->MakePrivateDirectory(clients));
  struct stat info{};
  ASSERT_EQ(stat(clients.c_str(), &info), 0);
  EXPECT_THAT(info.st_mode & static_cast<mode_t>(0777), Eq(0700U));

  ASSERT_TRUE(io_->CreatePrivate(clients + "/mcp", "token\n"));
  EXPECT_FALSE(io_->CreatePrivate(clients + "/mcp", "again\n"));
  ASSERT_EQ(stat((clients + "/mcp").c_str(), &info), 0);
  EXPECT_THAT(info.st_mode & static_cast<mode_t>(0777), Eq(0600U));
  std::stringstream contents;
  contents << std::ifstream(clients + "/mcp").rdbuf();
  EXPECT_THAT(contents.str(), Eq("token\n"));
  ASSERT_TRUE(io_->CreatePrivate(clients + "/.hidden", ""));

  EXPECT_THAT(io_->List(clients),
              testing::Optional(UnorderedElementsAre("mcp", ".hidden")));
  EXPECT_TRUE(io_->Remove(clients + "/mcp"));
  EXPECT_FALSE(io_->Remove(clients + "/mcp"));
}

// #114: a directory MakePrivateDirectory did not itself make -- left
// behind by an older build, or by a looser umask than the daemon's own
// explicit mode -- is put back to 0700 rather than trusted as is.
TEST_F(PosixApprovalsIoTest, FixesAnExistingDirectoryLeftLoose) {
  const std::string clients = root_ + "/state/facepalm/clients";
  ASSERT_TRUE(io_->MakePrivateDirectory(clients));
  ASSERT_EQ(chmod(clients.c_str(), 0755), 0);

  ASSERT_TRUE(io_->MakePrivateDirectory(clients));

  struct stat info{};
  ASSERT_EQ(stat(clients.c_str(), &info), 0);
  EXPECT_THAT(info.st_mode & static_cast<mode_t>(0777), Eq(0700U));
}

TEST_F(PosixApprovalsIoTest, ReportsWhatItCannotDo) {
  EXPECT_THAT(io_->List(root_ + "/missing"), Eq(std::nullopt));
  ASSERT_TRUE(io_->CreatePrivate(root_ + "/file", ""));
  EXPECT_FALSE(io_->MakePrivateDirectory(root_ + "/file"));
  EXPECT_FALSE(io_->MakePrivateDirectory(root_ + "/file/below"));
  EXPECT_FALSE(io_->MakePrivateDirectory(""));
  const std::string locked = root_ + "/locked";
  ASSERT_TRUE(io_->MakePrivateDirectory(locked));
  ASSERT_EQ(chmod(locked.c_str(), 0500), 0);
  EXPECT_FALSE(io_->MakePrivateDirectory(locked + "/below"));
  ASSERT_EQ(chmod(locked.c_str(), 0700), 0);
  EXPECT_FALSE(io_->CreatePrivate(root_ + "/missing/file", ""));
}

TEST_F(PosixApprovalsIoTest, GivesRandomBytes) {
  const std::optional<std::string> first = io_->Random(32);
  const std::optional<std::string> second = io_->Random(32);

  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_THAT(first.value_or("").size(), Eq(32U));
  EXPECT_NE(first.value_or(""), second.value_or(""));
  EXPECT_THAT(io_->Random(257), Eq(std::nullopt));
}

}  // namespace
}  // namespace facepalm
