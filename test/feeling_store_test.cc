#include "src/feeling_store.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/feelings.h"

// Each test stands for the specs/FeelingFeedback.tla property it is named
// for, or the file's own safety: the store is the only writer of the
// person's counts, and a save renames a synced temporary file over them.

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetErrnoAndReturn;

constexpr int kLockFd = 7;
constexpr int kDataFd = 8;
constexpr int kTempFd = 9;
constexpr int kDirectoryFd = 10;
constexpr uid_t kUser = 42;

// A frown at a question.
Evidence Frown() { return {.sign = "brows_down", .beat = BeatKind::kQuestion}; }
constexpr std::string_view kTuned =
    "facepalm feelings 3\n"
    "brows_down question unknown unknown unknown unknown annoyance 3\n";

class MockFeelingStoreIo : public FeelingStoreIo {
 public:
  MOCK_METHOD(int, MakeDirectory, (const std::string&), (override));
  MOCK_METHOD(int, Open, (const std::string&, int), (override));
  MOCK_METHOD(int, Lock, (int), (override));
  MOCK_METHOD(int, Stat, (int, struct stat&), (override));
  MOCK_METHOD(uid_t, User, (), (override));
  MOCK_METHOD(ssize_t, Read, (int, void*, std::size_t), (override));
  MOCK_METHOD(ssize_t, Write, (int, const void*, std::size_t), (override));
  MOCK_METHOD(int, Sync, (int), (override));
  MOCK_METHOD(int, Rename, (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(int, Unlink, (const std::string&), (override));
  MOCK_METHOD(void, Close, (int), (override));
};

// A private regular file of `size` bytes, owned by kUser.
int PrivateFile(struct stat& info, off_t size) {
  info.st_mode = S_IFREG | S_IRUSR | S_IWUSR;
  info.st_uid = kUser;
  info.st_size = size;
  return 0;
}

// An Io in which everything works: the folder exists, the lock is free,
// the data file holds `contents`, and every save succeeds.
std::shared_ptr<NiceMock<MockFeelingStoreIo>> GoodIo(
    const std::string& contents = {}) {
  auto io = std::make_shared<NiceMock<MockFeelingStoreIo>>();
  // A test's own expectations on a call leave its other calls allowed.
  EXPECT_CALL(*io, MakeDirectory).Times(AnyNumber());
  EXPECT_CALL(*io, Open).Times(AnyNumber());
  EXPECT_CALL(*io, Lock).Times(AnyNumber());
  EXPECT_CALL(*io, Read).Times(AnyNumber());
  EXPECT_CALL(*io, Write).Times(AnyNumber());
  EXPECT_CALL(*io, Sync).Times(AnyNumber());
  EXPECT_CALL(*io, Rename).Times(AnyNumber());
  EXPECT_CALL(*io, Close).Times(AnyNumber());
  ON_CALL(*io, MakeDirectory).WillByDefault(SetErrnoAndReturn(EEXIST, -1));
  ON_CALL(*io, Open("/state/feelings.lock", _)).WillByDefault(Return(kLockFd));
  ON_CALL(*io, Open("/state/feelings.tmp", _)).WillByDefault(Return(kTempFd));
  ON_CALL(*io, Open("/state", _)).WillByDefault(Return(kDirectoryFd));
  if (contents.empty()) {
    ON_CALL(*io, Open("/state/feelings", _))
        .WillByDefault(SetErrnoAndReturn(ENOENT, -1));
  } else {
    ON_CALL(*io, Open("/state/feelings", _)).WillByDefault(Return(kDataFd));
  }
  ON_CALL(*io, User).WillByDefault(Return(kUser));
  ON_CALL(*io, Stat).WillByDefault(
      [size = contents.size()](int fd, struct stat& info) {
        return PrivateFile(info, fd == kDataFd ? static_cast<off_t>(size) : 0);
      });
  ON_CALL(*io, Read).WillByDefault(
      [text = std::make_shared<const std::string>(contents)](int, void* buffer,
                                                             std::size_t size) {
        const std::size_t read = std::min(size, text->size());
        std::memcpy(buffer, text->data(), read);
        return static_cast<ssize_t>(read);
      });
  ON_CALL(*io, Write).WillByDefault([](int, const void*, std::size_t size) {
    return static_cast<ssize_t>(size);
  });
  return io;
}

std::unique_ptr<FeelingStore> Opened(
    const std::shared_ptr<NiceMock<MockFeelingStoreIo>>& io) {
  std::string error;
  auto store = FeelingStore::Open("/state/feelings", error, io);
  EXPECT_THAT(error, IsEmpty());
  return store;
}

// The error opening the store over `io` gives, failing if it opens.
std::string Refused(const std::shared_ptr<NiceMock<MockFeelingStoreIo>>& io,
                    const std::string& path = "/state/feelings") {
  std::string error;
  EXPECT_FALSE(FeelingStore::Open(path, error, io));
  return error;
}

TEST(FeelingStoreOpenTest, AMissingFileHoldsNoPicks) {
  const auto io = GoodIo();
  const auto store = Opened(io);

  ASSERT_TRUE(store);
  EXPECT_THAT(store->Read(Frown(), Feeling::kConfused), Eq(Feeling::kConfused));
}

TEST(FeelingStoreOpenTest, ReadsThePicksTheFileHolds) {
  const auto io = GoodIo(std::string(kTuned));
  EXPECT_CALL(*io, Close(kDataFd));
  const auto store = Opened(io);

  ASSERT_TRUE(store);
  EXPECT_THAT(store->Read(Frown(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

TEST(FeelingStoreOpenTest, MakesTheFoldersItsFileNeeds) {
  const auto io = GoodIo();
  {
    InSequence order;
    EXPECT_CALL(*io, MakeDirectory("/home/me/.config/facepalm"))
        .WillOnce(SetErrnoAndReturn(ENOENT, -1));
    EXPECT_CALL(*io, MakeDirectory("/home/me/.config"))
        .WillOnce(SetErrnoAndReturn(ENOENT, -1));
    EXPECT_CALL(*io, MakeDirectory("/home/me")).WillOnce(Return(0));
    EXPECT_CALL(*io, MakeDirectory("/home/me/.config")).WillOnce(Return(0));
    EXPECT_CALL(*io, MakeDirectory("/home/me/.config/facepalm"))
        .WillOnce(SetErrnoAndReturn(EEXIST, -1));
  }
  ON_CALL(*io, Open).WillByDefault(Return(kLockFd));
  ON_CALL(*io, Open("/home/me/.config/facepalm/feelings", _))
      .WillByDefault(SetErrnoAndReturn(ENOENT, -1));
  std::string error;

  EXPECT_TRUE(
      FeelingStore::Open("/home/me/.config/facepalm/feelings", error, io));
}

TEST(FeelingStoreOpenTest, AFileInTheWorkingFolderNeedsNoFolderMade) {
  const auto io = GoodIo();
  EXPECT_CALL(*io, MakeDirectory(".")).WillOnce(SetErrnoAndReturn(EEXIST, -1));
  ON_CALL(*io, Open).WillByDefault(Return(kLockFd));
  ON_CALL(*io, Open("feelings", _))
      .WillByDefault(SetErrnoAndReturn(ENOENT, -1));
  std::string error;

  EXPECT_TRUE(FeelingStore::Open("feelings", error, io));
}

TEST(FeelingStoreOpenTest, AFileAtTheRootNeedsNoFolderMade) {
  const auto io = GoodIo();
  EXPECT_CALL(*io, MakeDirectory("/")).WillOnce(SetErrnoAndReturn(EEXIST, -1));
  ON_CALL(*io, Open).WillByDefault(Return(kLockFd));
  ON_CALL(*io, Open("/feelings", _))
      .WillByDefault(SetErrnoAndReturn(ENOENT, -1));
  std::string error;

  EXPECT_TRUE(FeelingStore::Open("/feelings", error, io));
}

TEST(FeelingStoreOpenTest, RefusesAFolderItCannotMake) {
  {
    const auto io = GoodIo();
    EXPECT_CALL(*io, MakeDirectory("/state"))
        .WillOnce(SetErrnoAndReturn(EACCES, -1));
    EXPECT_THAT(Refused(io), HasSubstr("folder"));
  }
  {
    // The root's own child, missing: there is nothing above it to make.
    const auto io = GoodIo();
    EXPECT_CALL(*io, MakeDirectory("/state"))
        .WillOnce(SetErrnoAndReturn(ENOENT, -1));
    EXPECT_THAT(Refused(io), HasSubstr("folder"));
  }
  {
    const auto io = GoodIo();
    EXPECT_CALL(*io, MakeDirectory("dir"))
        .WillOnce(SetErrnoAndReturn(ENOENT, -1));
    EXPECT_THAT(Refused(io, "dir/feelings"), HasSubstr("folder"));
  }
  {
    const auto io = GoodIo();
    EXPECT_CALL(*io, MakeDirectory("/a/b"))
        .WillOnce(SetErrnoAndReturn(ENOENT, -1))
        .WillOnce(SetErrnoAndReturn(EACCES, -1));
    EXPECT_CALL(*io, MakeDirectory("/a")).WillOnce(Return(0));
    EXPECT_THAT(Refused(io, "/a/b/feelings"), HasSubstr("folder"));
  }
}

TEST(FeelingStoreOpenTest, RefusesALockFileItCannotOpen) {
  const auto io = GoodIo();
  EXPECT_CALL(*io, Open("/state/feelings.lock", _)).WillOnce(Return(-1));

  EXPECT_THAT(Refused(io), Eq("cannot open the feelings file"));
}

// FeelingFeedback's one writer, across processes: a second daemon cannot
// take the lock.
TEST(FeelingStoreOpenTest, RefusesALockAnotherWriterHolds) {
  const auto io = GoodIo();
  EXPECT_CALL(*io, Lock(kLockFd)).WillOnce(Return(-1));
  EXPECT_CALL(*io, Close(kLockFd));

  EXPECT_THAT(Refused(io), Eq("the feelings file is unsafe or in use"));
}

TEST(FeelingStoreOpenTest, RefusesALockOrDataFileThatIsNotPrivate) {
  for (const int fd : {kLockFd, kDataFd}) {
    for (int failure = 0; failure < 4; ++failure) {
      const auto io = GoodIo(std::string(kTuned));
      ON_CALL(*io, Stat(fd, _))
          .WillByDefault([failure](int, struct stat& info) {
            PrivateFile(info, 1);
            if (failure == 1) {
              info.st_mode = S_IFDIR | S_IRWXU;
            } else if (failure == 2) {
              info.st_uid = kUser + 1;
            } else if (failure == 3) {
              info.st_mode |= S_IRGRP;
            }
            return failure == 0 ? -1 : 0;
          });
      EXPECT_CALL(*io, Close(kLockFd));
      EXPECT_FALSE(Refused(io).empty()) << fd << ' ' << failure;
    }
  }
}

TEST(FeelingStoreOpenTest, RefusesADataFileItCannotOpen) {
  const auto io = GoodIo();
  EXPECT_CALL(*io, Open("/state/feelings", _))
      .WillOnce(SetErrnoAndReturn(EACCES, -1));
  EXPECT_CALL(*io, Close(kLockFd));

  EXPECT_THAT(Refused(io), Eq("the feelings file is unsafe or not facepalm's"));
}

TEST(FeelingStoreOpenTest, RefusesADataFileTooLargeShortOrNotFacepalms) {
  {
    const auto io = GoodIo(std::string(kTuned));
    ON_CALL(*io, Stat(kDataFd, _)).WillByDefault([](int, struct stat& info) {
      return PrivateFile(info, FeelingStore::kLargestFile + 1);
    });
    EXPECT_CALL(*io, Close(kDataFd));
    EXPECT_FALSE(Refused(io).empty());
  }
  {
    const auto io = GoodIo(std::string(kTuned));
    EXPECT_CALL(*io, Read(kDataFd, _, _)).WillOnce(Return(1));
    EXPECT_FALSE(Refused(io).empty());
  }
  {
    const auto io = GoodIo("not facepalm's\n");
    EXPECT_FALSE(Refused(io).empty());
  }
}

// #143 bumped the format from 1 to 2 for the added gaze word, and #145
// from 2 to 3 for blink rate, a long blink and widened eyes, but a file a
// pre-#143 or pre-#145 daemon saved is read, not refused: refusing it
// would throw away the person's own picks for a file they never asked to
// change, unlike a calibration profile, which the person simply redoes
// (#137's own version bump). A v1 file's picks read with every context
// field unknown, and the next save writes the file back as v3.
TEST(FeelingStoreOpenTest, ReadsAV1FileAndSavesItBackAsV3) {
  const auto io =
      GoodIo("facepalm feelings 1\nbrows_down question annoyance 3\n");
  const auto store = Opened(io);
  ASSERT_TRUE(store);
  EXPECT_THAT(store->Read(Frown(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
  std::string written;
  EXPECT_CALL(*io, Write(kTempFd, _, _))
      .WillOnce([&](int, const void* data, std::size_t size) {
        written.assign(static_cast<const char*>(data), size);
        return static_cast<ssize_t>(size);
      });

  EXPECT_TRUE(store->Learn(Frown(), Feeling::kAnnoyance));

  EXPECT_THAT(written, Eq("facepalm feelings 3\n"
                          "brows_down question unknown unknown unknown unknown "
                          "annoyance 4\n"));
}

// A v2 file's picks keep their own gaze, and read the three fields #145
// added as unknown; the next save writes the file back as v3 too.
TEST(FeelingStoreOpenTest, ReadsAV2FileAndSavesItBackAsV3) {
  const auto io = GoodIo(
      "facepalm feelings 2\nbrows_down question on_content annoyance 3\n");
  const auto store = Opened(io);
  ASSERT_TRUE(store);
  EXPECT_THAT(store->Read({.sign = "brows_down",
                           .beat = BeatKind::kQuestion,
                           .gaze = Gaze::kThisScreen},
                          Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
  std::string written;
  EXPECT_CALL(*io, Write(kTempFd, _, _))
      .WillOnce([&](int, const void* data, std::size_t size) {
        written.assign(static_cast<const char*>(data), size);
        return static_cast<ssize_t>(size);
      });

  EXPECT_TRUE(store->Learn({.sign = "brows_down",
                            .beat = BeatKind::kQuestion,
                            .gaze = Gaze::kThisScreen},
                           Feeling::kAnnoyance));

  EXPECT_THAT(written,
              Eq("facepalm feelings 3\n"
                 "brows_down question on_content unknown unknown unknown "
                 "annoyance 4\n"));
}

// FeelingFeedback!Begin to ReplyOk: a pick is saved by renaming a synced
// temporary file over the old one, then syncing the folder.
TEST(AckedKeptTest, APickIsAcknowledgedOnlyOnceItsSaveIsDurable) {
  const auto io = GoodIo();
  const auto store = Opened(io);
  ASSERT_TRUE(store);
  std::string written;
  {
    InSequence order;
    EXPECT_CALL(*io, Unlink("/state/feelings.tmp"));
    EXPECT_CALL(*io, Open("/state/feelings.tmp",
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC))
        .WillOnce(Return(kTempFd));
    EXPECT_CALL(*io, Write(kTempFd, _, _))
        .WillOnce([&](int, const void* data, std::size_t size) {
          written.assign(static_cast<const char*>(data), size);
          return static_cast<ssize_t>(size);
        });
    EXPECT_CALL(*io, Sync(kTempFd)).WillOnce(Return(0));
    EXPECT_CALL(*io, Close(kTempFd));
    EXPECT_CALL(*io, Rename("/state/feelings.tmp", "/state/feelings"))
        .WillOnce(Return(0));
    EXPECT_CALL(*io, Open("/state", O_RDONLY | O_DIRECTORY | O_CLOEXEC))
        .WillOnce(Return(kDirectoryFd));
    EXPECT_CALL(*io, Sync(kDirectoryFd)).WillOnce(Return(0));
    EXPECT_CALL(*io, Close(kDirectoryFd));
  }

  EXPECT_TRUE(store->Learn(Frown(), Feeling::kAnnoyance));
  EXPECT_THAT(written, Eq("facepalm feelings 3\n"
                          "brows_down question unknown unknown unknown unknown "
                          "annoyance 1\n"));
}

// The tuning follows the saved picks: the third pick changes the read.
TEST(ReadsTheFileTest, ThePicksTuneTheNextRead) {
  const auto io = GoodIo();
  const auto store = Opened(io);
  ASSERT_TRUE(store);

  EXPECT_TRUE(store->Learn(Frown(), Feeling::kAnnoyance));
  EXPECT_TRUE(store->Learn(Frown(), Feeling::kAnnoyance));
  EXPECT_THAT(store->Read(Frown(), Feeling::kConfused), Eq(Feeling::kConfused));
  EXPECT_TRUE(store->Learn(Frown(), Feeling::kAnnoyance));
  EXPECT_THAT(store->Read(Frown(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

// Makes save step `step` of `io` fail: opening, writing, syncing the
// temporary file, or the rename.
void FailSave(NiceMock<MockFeelingStoreIo>& io, int step) {
  if (step == 0) {
    EXPECT_CALL(io, Open("/state/feelings.tmp", _)).WillOnce(Return(-1));
    return;
  }
  EXPECT_CALL(io, Close(kTempFd));
  if (step == 1) {
    EXPECT_CALL(io, Write(kTempFd, _, _)).WillOnce(Return(1));
  } else if (step == 2) {
    EXPECT_CALL(io, Sync(kTempFd)).WillOnce(Return(-1));
  } else {
    EXPECT_CALL(io, Rename).WillOnce(Return(-1));
  }
}

// FeelingFeedback!Fail before the rename: the pick is neither acknowledged
// nor read (GuardCommitOnRename).
TEST(ReadsTheFileTest, APickWhoseSaveFailsIsNotRead) {
  for (int step = 0; step < 4; ++step) {
    const auto io = GoodIo(
        "facepalm feelings 3\n"
        "brows_down question unknown unknown unknown unknown annoyance 2\n");
    const auto store = Opened(io);
    ASSERT_TRUE(store);
    FailSave(*io, step);

    EXPECT_FALSE(store->Learn(Frown(), Feeling::kAnnoyance));
    EXPECT_THAT(store->Read(Frown(), Feeling::kConfused),
                Eq(Feeling::kConfused))
        << step;
  }
}

// FeelingFeedback!Fail after the rename: the folder is not synced, so the
// pick is not acknowledged, but the file holds it, so it is read.
TEST(ReadsTheFileTest, APickRenamedInButNotDurableIsReadButNotAcknowledged) {
  for (const bool opens : {false, true}) {
    const auto io = GoodIo(
        "facepalm feelings 3\n"
        "brows_down question unknown unknown unknown unknown annoyance 2\n");
    const auto store = Opened(io);
    ASSERT_TRUE(store);
    if (opens) {
      EXPECT_CALL(*io, Sync(kDirectoryFd)).WillOnce(Return(-1));
      EXPECT_CALL(*io, Close(kDirectoryFd));
    } else {
      EXPECT_CALL(*io, Open("/state", _)).WillOnce(Return(-1));
    }

    EXPECT_FALSE(store->Learn(Frown(), Feeling::kAnnoyance));
    EXPECT_THAT(store->Read(Frown(), Feeling::kConfused),
                Eq(Feeling::kAnnoyance));
  }
}

// FeelingFeedback!Reset: a reset saves empty counts.
TEST(ResetForgetsTest, ForgettingSavesNoPicksAndRestoresTheGenericRead) {
  const auto io = GoodIo(std::string(kTuned));
  const auto store = Opened(io);
  ASSERT_TRUE(store);
  std::string written;
  EXPECT_CALL(*io, Write(kTempFd, _, _))
      .WillOnce([&](int, const void* data, std::size_t size) {
        written.assign(static_cast<const char*>(data), size);
        return static_cast<ssize_t>(size);
      });

  EXPECT_TRUE(store->Forget());
  EXPECT_THAT(written, Eq("facepalm feelings 3\n"));
  EXPECT_THAT(store->Read(Frown(), Feeling::kConfused), Eq(Feeling::kConfused));
}

TEST(ResetForgetsTest, AFailedResetKeepsThePicks) {
  const auto io = GoodIo(std::string(kTuned));
  const auto store = Opened(io);
  ASSERT_TRUE(store);
  EXPECT_CALL(*io, Rename).WillOnce(Return(-1));

  EXPECT_FALSE(store->Forget());
  EXPECT_THAT(store->Read(Frown(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
}

TEST(FeelingStoreTest, ClosingTheStoreReleasesItsLock) {
  const auto io = GoodIo();
  auto store = Opened(io);
  ASSERT_TRUE(store);
  EXPECT_CALL(*io, Close(kLockFd));

  store.reset();
}

// The environment's XDG_CONFIG_HOME and HOME, as std::getenv gives them.
std::function<const char*(const char*)> Environment(const char* config_home,
                                                    const char* home) {
  return [=](const char* name) {
    return std::string_view(name) == "XDG_CONFIG_HOME" ? config_home : home;
  };
}

TEST(DefaultFeelingsPathTest, IsInTheConfigFolder) {
  EXPECT_THAT(DefaultFeelingsPath(Environment("/x/config", "/home/me")),
              Eq("/x/config/facepalm/feelings"));
  EXPECT_THAT(DefaultFeelingsPath(Environment(nullptr, "/home/me")),
              Eq("/home/me/.config/facepalm/feelings"));
  EXPECT_THAT(DefaultFeelingsPath(Environment("", "/home/me")),
              Eq("/home/me/.config/facepalm/feelings"));
  EXPECT_THAT(DefaultFeelingsPath(Environment(nullptr, nullptr)), IsEmpty());
  EXPECT_THAT(DefaultFeelingsPath(Environment(nullptr, "")), IsEmpty());
}

// The POSIX calls themselves, on a real folder that each test removes.
class FeelingStoreFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::array<char, 32> name = {"/tmp/fp-feelings-XXXXXX"};
    ASSERT_NE(mkdtemp(name.data()), nullptr);
    folder_ = name.data();
    path_ = folder_ + "/state/feelings";
  }
  void TearDown() override { std::filesystem::remove_all(folder_); }

  [[nodiscard]] std::string Contents() const {
    std::ifstream file(path_);
    return {std::istreambuf_iterator<char>(file), {}};
  }

  std::string folder_;
  std::string path_;
};

// Checks the file's permissions, its folder's, and that a pick survives a
// reopen and a forget clears it. Linux's libstdc++ scores this higher than
// macOS's libc++ does for the same source.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(FeelingStoreFileTest, KeepsPicksInAPrivateFileAcrossRuns) {
  std::string error;
  {
    auto store = FeelingStore::Open(path_, error);
    ASSERT_TRUE(store) << error;
    for (int pick = 0; pick < 3; ++pick) {
      ASSERT_TRUE(store->Learn(Frown(), Feeling::kAnnoyance));
    }
  }
  EXPECT_THAT(Contents(), Eq(kTuned));
  struct stat info{};
  ASSERT_EQ(stat(path_.c_str(), &info), 0);
  EXPECT_THAT(info.st_mode & static_cast<mode_t>(S_IRWXU | S_IRWXG | S_IRWXO),
              Eq(static_cast<mode_t>(S_IRUSR | S_IWUSR)));
  ASSERT_EQ(stat((folder_ + "/state").c_str(), &info), 0);
  EXPECT_THAT(info.st_mode & static_cast<mode_t>(S_IRWXG | S_IRWXO), Eq(0U));

  const auto again = FeelingStore::Open(path_, error);

  ASSERT_TRUE(again) << error;
  EXPECT_THAT(again->Read(Frown(), Feeling::kConfused),
              Eq(Feeling::kAnnoyance));
  EXPECT_TRUE(again->Forget());
  EXPECT_THAT(Contents(), Eq("facepalm feelings 3\n"));
}

// FeelingFeedback's GuardOneWriter, within the daemon: picks from several
// connections at once are saved one at a time, and none is lost.
TEST_F(FeelingStoreFileTest, PicksMadeAtOnceAreAllKept) {
  constexpr int kPicks = 20;
  std::string error;
  const auto store = FeelingStore::Open(path_, error);
  ASSERT_TRUE(store) << error;
  const auto pick = [&] {
    for (int i = 0; i < kPicks; ++i) {
      EXPECT_TRUE(store->Learn(Frown(), Feeling::kAnnoyance));
    }
  };

  std::thread first(pick);
  std::thread second(pick);
  first.join();
  second.join();

  EXPECT_THAT(Contents(),
              Eq("facepalm feelings 3\n"
                 "brows_down question unknown unknown unknown unknown "
                 "annoyance " +
                 std::to_string(2 * kPicks) + "\n"));
}

TEST_F(FeelingStoreFileTest, ASecondStoreCannotOpenTheSameFile) {
  std::string error;
  const auto first = FeelingStore::Open(path_, error);
  ASSERT_TRUE(first) << error;

  EXPECT_FALSE(FeelingStore::Open(path_, error));
  EXPECT_THAT(error, Eq("the feelings file is unsafe or in use"));
}

TEST_F(FeelingStoreFileTest, RefusesALinkInPlaceOfTheFile) {
  std::string error;
  ASSERT_TRUE(FeelingStore::Open(path_, error)) << error;
  const std::string target = folder_ + "/elsewhere";
  std::ofstream(target) << kTuned;
  ASSERT_EQ(chmod(target.c_str(), S_IRUSR | S_IWUSR), 0);
  ASSERT_EQ(symlink(target.c_str(), path_.c_str()), 0);

  EXPECT_FALSE(FeelingStore::Open(path_, error));
}

TEST_F(FeelingStoreFileTest, RefusesAFileOthersCanRead) {
  std::string error;
  ASSERT_TRUE(FeelingStore::Open(path_, error)) << error;
  std::ofstream(path_) << kTuned;
  ASSERT_EQ(chmod(path_.c_str(), S_IRUSR | S_IWUSR | S_IROTH), 0);

  EXPECT_FALSE(FeelingStore::Open(path_, error));
}

}  // namespace
}  // namespace facepalm
