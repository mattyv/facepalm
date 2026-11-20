#include "src/rating_log.h"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {
class RatingLogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::array<char, 32> temp = {"/tmp/fp-rating-XXXXXX"};
    const int fd = mkstemp(temp.data());
    ASSERT_GE(fd, 0);
    close(fd);
    path_ = temp.data();
  }
  void TearDown() override { std::remove(path_.c_str()); }
  std::string path_;
};

class MockRatingLogIo : public RatingLogIo {
 public:
  MOCK_METHOD(int, OpenFile, (const std::string&), (override));
  MOCK_METHOD(int, OpenDirectory, (const std::string&), (override));
  MOCK_METHOD(int, Stat, (int, struct stat&), (override));
  MOCK_METHOD(uid_t, User, (), (override));
  MOCK_METHOD(int, Lock, (int), (override));
  MOCK_METHOD(ssize_t, ReadAt, (int, void*, std::size_t, off_t), (override));
  MOCK_METHOD(ssize_t, Write, (int, const void*, std::size_t), (override));
  MOCK_METHOD(int, Sync, (int), (override));
  MOCK_METHOD(void, Close, (int), (override));
};

std::shared_ptr<testing::NiceMock<MockRatingLogIo>> GoodIo() {
  auto io = std::make_shared<testing::NiceMock<MockRatingLogIo>>();
  ON_CALL(*io, OpenFile).WillByDefault(testing::Return(7));
  ON_CALL(*io, OpenDirectory).WillByDefault(testing::Return(8));
  ON_CALL(*io, Lock).WillByDefault(testing::Return(0));
  ON_CALL(*io, User).WillByDefault(testing::Return(42));
  ON_CALL(*io, Sync).WillByDefault(testing::Return(0));
  ON_CALL(*io, Stat).WillByDefault([](int, struct stat& value) {
    value.st_mode = S_IFREG | 0600;
    value.st_uid = 42;
    value.st_size = 0;
    return 0;
  });
  ON_CALL(*io, Write).WillByDefault([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  return io;
}

TEST(RatingLogIoTest, RefusesOpenMetadataAndDirectoryFailures) {
  std::string error;
  {
    auto const io = GoodIo();
    EXPECT_CALL(*io, OpenFile).WillOnce(testing::Return(-1));
    EXPECT_FALSE(RatingLog::Open("file", error, io));
  }
  {
    auto const io = GoodIo();
    EXPECT_CALL(*io, OpenDirectory(".")).WillOnce(testing::Return(-1));
    EXPECT_FALSE(RatingLog::Open("file", error, io));
  }
  {
    auto const io = GoodIo();
    EXPECT_CALL(*io, OpenDirectory("/")).WillOnce(testing::Return(8));
    EXPECT_CALL(*io, Sync(8)).WillOnce(testing::Return(-1));
    EXPECT_FALSE(RatingLog::Open("/file", error, io));
  }
  for (int failure = 0; failure < 5; ++failure) {
    auto const io = GoodIo();
    EXPECT_CALL(*io, Stat).WillOnce([failure](int, struct stat& value) {
      value.st_mode = S_IFREG | 0600;
      value.st_uid = 42;
      value.st_size = 0;
      if (failure == 0) {
        return -1;
      }
      if (failure == 1) {
        value.st_mode = S_IFDIR | 0700;
      }
      if (failure == 2) {
        value.st_uid = 43;
      }
      if (failure == 3) {
        value.st_size = -1;
      }
      if (failure == 4) {
        value.st_size = (10LL * 1024 * 1024) + 1;
      }
      return 0;
    });
    EXPECT_FALSE(RatingLog::Open("/tmp/file", error, io));
  }
}

// This exercises independent I/O failure paths with one mock boundary.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(RatingLogIoTest, HandlesReadAndAppendFailures) {
  std::string error;
  {
    auto const io = GoodIo();
    EXPECT_CALL(*io, Stat).WillOnce([](int, struct stat& value) {
      value.st_mode = S_IFREG | 0600;
      value.st_uid = 42;
      value.st_size = 30;
      return 0;
    });
    EXPECT_CALL(*io, ReadAt).WillOnce(testing::Return(-1));
    EXPECT_FALSE(RatingLog::Open("/tmp/file", error, io));
  }
  for (int failure = 0; failure < 2; ++failure) {
    auto const io = GoodIo();
    EXPECT_CALL(*io, Stat)
        .WillOnce([](int, struct stat& value) {
          value.st_mode = S_IFREG | 0600;
          value.st_uid = 42;
          value.st_size = 0;
          return 0;
        })
        .WillOnce([failure](int, struct stat& value) {
          value.st_size = -1;
          return failure == 0 ? -1 : 0;
        });
    auto log = RatingLog::Open("/tmp/file", error, io);
    ASSERT_TRUE(log);
    EXPECT_FALSE(log->Record({.client = "test",
                              .id = 1,
                              .score = 4,
                              .summary = "Gesture: nod",
                              .published_at_ms = 1234}));
  }
  {
    auto const io = GoodIo();
    EXPECT_CALL(*io, Write)
        .WillOnce([](int, const void*, std::size_t) {
          errno = EINTR;
          return -1;
        })
        .WillOnce([](int, const void*, std::size_t size) {
          return static_cast<ssize_t>(size);
        });
    auto log = RatingLog::Open("/tmp/file", error, io);
    ASSERT_TRUE(log);
    EXPECT_TRUE(log->Record({.client = "test",
                             .id = 1,
                             .score = 4,
                             .summary = "Gesture: nod",
                             .published_at_ms = 1234}));
  }
  {
    auto const io = GoodIo();
    EXPECT_CALL(*io, Write).WillOnce([](int, const void*, std::size_t) {
      errno = EIO;
      return -1;
    });
    auto log = RatingLog::Open("/tmp/file", error, io);
    ASSERT_TRUE(log);
    EXPECT_FALSE(log->Record({.client = "test",
                              .id = 1,
                              .score = 4,
                              .summary = "Gesture: nod",
                              .published_at_ms = 1234}));
  }
}

TEST(RatingLogIoTest, RejectsFailureReadingFinalByte) {
  const auto io = GoodIo();
  EXPECT_CALL(*io, Stat).WillOnce([](int, struct stat& value) {
    value.st_mode = S_IFREG | 0600;
    value.st_uid = 42;
    value.st_size = 30;
    return 0;
  });
  EXPECT_CALL(*io, ReadAt)
      .WillOnce([](int, void* buffer, std::size_t size, off_t) {
        constexpr std::string_view kPrefix = R"({"version":1,)";
        std::memcpy(buffer, kPrefix.data(), size);
        return static_cast<ssize_t>(size);
      })
      .WillOnce(testing::Return(-1));
  std::string error;
  EXPECT_FALSE(RatingLog::Open("/tmp/file", error, io));
}

TEST(RatingLogIoTest, PartialWritePoisonsSinkAndNeverAcknowledges) {
  auto const io = std::make_shared<MockRatingLogIo>();
  EXPECT_CALL(*io, OpenFile).WillOnce(testing::Return(7));
  EXPECT_CALL(*io, Lock(7)).WillOnce(testing::Return(0));
  EXPECT_CALL(*io, Stat(7, testing::_))
      .Times(2)
      .WillRepeatedly([](int, struct stat& value) {
        value.st_mode = S_IFREG | 0600;
        value.st_uid = 42;
        value.st_size = 0;
        return 0;
      });
  EXPECT_CALL(*io, User).WillOnce(testing::Return(42));
  EXPECT_CALL(*io, OpenDirectory).WillOnce(testing::Return(8));
  EXPECT_CALL(*io, Sync(8)).WillOnce(testing::Return(0));
  EXPECT_CALL(*io, Close(8));
  EXPECT_CALL(*io, Close(7));
  EXPECT_CALL(*io, Write(7, testing::_, testing::_))
      .WillOnce(testing::Return(1));
  std::string error;
  auto log = RatingLog::Open("/tmp/test", error, io);
  ASSERT_TRUE(log);
  ReactionFeed::Rating const rating{.client = "test",
                                    .id = 1,
                                    .score = 4,
                                    .summary = "Gesture: nod",
                                    .published_at_ms = 1234};
  EXPECT_FALSE(log->Record(rating));
  EXPECT_FALSE(log->Record(rating));
}

TEST(RatingLogIoTest, SyncFailurePoisonsSink) {
  auto const io = std::make_shared<MockRatingLogIo>();
  EXPECT_CALL(*io, OpenFile).WillOnce(testing::Return(7));
  EXPECT_CALL(*io, Lock(7)).WillOnce(testing::Return(0));
  EXPECT_CALL(*io, Stat(7, testing::_))
      .Times(2)
      .WillRepeatedly([](int, struct stat& value) {
        value.st_mode = S_IFREG | 0600;
        value.st_uid = 42;
        value.st_size = 0;
        return 0;
      });
  EXPECT_CALL(*io, User).WillOnce(testing::Return(42));
  EXPECT_CALL(*io, OpenDirectory).WillOnce(testing::Return(8));
  EXPECT_CALL(*io, Sync(8)).WillOnce(testing::Return(0));
  EXPECT_CALL(*io, Sync(7)).WillOnce(testing::Return(-1));
  EXPECT_CALL(*io, Close(8));
  EXPECT_CALL(*io, Close(7));
  EXPECT_CALL(*io, Write(7, testing::_, testing::_))
      .WillOnce([](int, const void*, std::size_t size) {
        return static_cast<ssize_t>(size);
      });
  std::string error;
  auto log = RatingLog::Open("/tmp/test", error, io);
  ASSERT_TRUE(log);
  ReactionFeed::Rating const rating{.client = "test",
                                    .id = 1,
                                    .score = 4,
                                    .summary = "Gesture: nod",
                                    .published_at_ms = 1234};
  EXPECT_FALSE(log->Record(rating));
  EXPECT_FALSE(log->Record(rating));
}

TEST_F(RatingLogTest, RejectsInvalidRatingsAndFullFile) {
  std::string error;
  auto log = RatingLog::Open(path_, error);
  ASSERT_TRUE(log);
  ReactionFeed::Rating rating{.client = "test",
                              .id = 1,
                              .score = 4,
                              .summary = "Gesture: nod",
                              .published_at_ms = 1234};
  rating.id = 0;
  EXPECT_FALSE(log->Record(rating));
  rating.id = 1;
  rating.score = 0;
  EXPECT_FALSE(log->Record(rating));
  rating.score = 6;
  EXPECT_FALSE(log->Record(rating));
  rating.score = 4;
  rating.summary.clear();
  EXPECT_FALSE(log->Record(rating));
  rating.summary = std::string(257, 'x');
  EXPECT_FALSE(log->Record(rating));
  rating.summary = "Gesture: nod";
  rating.published_at_ms = 0;
  EXPECT_FALSE(log->Record(rating));
  rating.published_at_ms = 1234;
  ASSERT_EQ(truncate(path_.c_str(), 10LL * 1024 * 1024), 0);
  EXPECT_FALSE(log->Record(rating));
}

TEST_F(RatingLogTest, RejectsSymbolicLink) {
  const std::string link = path_ + ".link";
  ASSERT_EQ(symlink(path_.c_str(), link.c_str()), 0);
  std::string error;
  EXPECT_FALSE(RatingLog::Open(link, error));
  std::remove(link.c_str());
}

TEST_F(RatingLogTest, AppendsOnlyInterpretedReadingAndDurablyAcknowledges) {
  std::string error;
  auto log = RatingLog::Open(path_, error);
  ASSERT_TRUE(log);
  EXPECT_TRUE(log->Record({.client = "do-not-store",
                           .id = 4,
                           .score = 5,
                           .summary = "Gesture: nod",
                           .published_at_ms = 1234}));
  std::ifstream file(path_);
  std::string text((std::istreambuf_iterator<char>(file)), {});
  EXPECT_THAT(text, testing::HasSubstr("\"version\":1"));
  EXPECT_THAT(text, testing::HasSubstr("\"summary\":\"Gesture: nod\""));
  EXPECT_THAT(text, testing::HasSubstr("\"published_at_ms\":1234"));
  EXPECT_THAT(text, testing::HasSubstr("\"score\":5"));
  EXPECT_THAT(text, testing::Not(testing::HasSubstr("do-not-store")));
  EXPECT_EQ(text.back(), '\n');
}

TEST_F(RatingLogTest, RejectsSharedPermissionsAndIncompleteRecord) {
  ASSERT_EQ(chmod(path_.c_str(), 0644), 0);
  std::string error;
  EXPECT_FALSE(RatingLog::Open(path_, error));
  ASSERT_EQ(chmod(path_.c_str(), 0600), 0);
  std::ofstream(path_) << "incomplete";
  EXPECT_FALSE(RatingLog::Open(path_, error));
}

TEST_F(RatingLogTest, ReopensACompleteLogButRejectsATruncatedOne) {
  std::string error;
  {
    auto log = RatingLog::Open(path_, error);
    ASSERT_TRUE(log);
    ASSERT_TRUE(log->Record({.client = "test",
                             .id = 1,
                             .score = 4,
                             .summary = "Gesture: nod",
                             .published_at_ms = 1234}));
  }
  EXPECT_TRUE(RatingLog::Open(path_, error));
  std::ofstream(path_, std::ios::app) << "partial";
  EXPECT_FALSE(RatingLog::Open(path_, error));
}

TEST_F(RatingLogTest, RejectsAnUnrelatedNewlineTerminatedFile) {
  std::ofstream(path_) << "not a feedback log\n";
  std::string error;
  EXPECT_FALSE(RatingLog::Open(path_, error));
}

TEST_F(RatingLogTest, RejectsSecondWriterAndMissingReadingTime) {
  std::string error;
  auto first = RatingLog::Open(path_, error);
  ASSERT_TRUE(first);
  EXPECT_FALSE(RatingLog::Open(path_, error));
  EXPECT_FALSE(first->Record({.client = "test",
                              .id = 1,
                              .score = 3,
                              .summary = "Gesture: nod",
                              .published_at_ms = std::nullopt}));
}
}  // namespace
}  // namespace facepalm
