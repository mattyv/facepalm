// Tests src/linux/v4l2_hotplug.cc with libv4l2 and inotify mocked: the fakes
// at the end of this file stand in for them at link time, so no camera is
// opened and no real directory is watched (#111). V4l2SessionTest fakes
// HotplugMonitor itself instead, so this is the only test that exercises
// V4l2InotifyMonitor's real watch loop.

#include "src/linux/v4l2_hotplug.h"

#include <fcntl.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;

// The libv4l2 and inotify calls V4l2InotifyMonitor makes, as a mock.
class MockV4l2Hotplug {
 public:
  MOCK_METHOD(int, InotifyInit1, (int flags));
  MOCK_METHOD(int, InotifyAddWatch,
              (int fd, const char* path, std::uint32_t mask));
  MOCK_METHOD(int, Select, (int nfds, timeval* timeout));
  MOCK_METHOD(ssize_t, Read, (int fd, void* buffer, std::size_t size));
  MOCK_METHOD(int, Open, (const char* file, int flags));
  MOCK_METHOD(int, Close, (int fd));
  MOCK_METHOD(int, QueryCap, (int fd, v4l2_capability* cap));
};

MockV4l2Hotplug* mock = nullptr;

// A deliberately large value, so the real close() this file never mocks
// (see below) cannot collide with one of the test process's own open
// descriptors when V4l2InotifyMonitor closes it.
constexpr int kNotifyFd = 4242;
constexpr int kFd = 7;
constexpr char kKnownCard[] = "USB Camera";
constexpr char kKnownBusInfo[] = "usb-0000:00:14.0-1";
constexpr auto kPatience = std::chrono::seconds(10);

// Answers VIDIOC_QUERYCAP with `card`/`bus_info`, as
// v4l2_session_test.cc's own Capability() does.
auto Capability(std::string card, std::string bus_info) {
  return [card = std::move(card), bus_info = std::move(bus_info)](
             int, v4l2_capability* cap) {
    std::memset(cap, 0, sizeof(*cap));
    std::memcpy(cap->card, card.c_str(), card.size());
    std::memcpy(cap->bus_info, bus_info.c_str(), bus_info.size());
    return 0;
  };
}

// A select() that waits a moment and finds nothing ready, the way
// v4l2_session_test.cc's NothingYet() slows its own busy loop.
int TimesOut(int, timeval*) {
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return 0;
}

// Encodes one inotify_event: its own fixed fields, followed by `name`,
// NUL-terminated. Real inotify also pads the name to align the event that
// follows; V4l2InotifyMonitor::Watch reads by `len` alone, so this fake
// does not need to.
std::vector<std::byte> EncodedEvent(std::uint32_t event_mask,
                                    const std::string& name) {
  inotify_event header{.wd = 1,
                       .mask = event_mask,
                       .cookie = 0,
                       .len = static_cast<std::uint32_t>(name.size() + 1)};
  std::vector<std::byte> bytes(sizeof(header) + header.len);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + sizeof(header), name.c_str(), header.len);
  return bytes;
}

std::vector<std::byte> EncodedEvents(
    std::initializer_list<std::vector<std::byte>> events) {
  std::vector<std::byte> all;
  for (const std::vector<std::byte>& event : events) {
    all.insert(all.end(), event.begin(), event.end());
  }
  return all;
}

// Fills the read buffer with `bytes` and returns how many there were, as
// v4l2_session_test.cc's own Delivers() does for a captured frame.
auto Delivers(const std::vector<std::byte>& bytes) {
  // NOLINTNEXTLINE(bugprone-exception-escape)
  return [bytes](int, void* buffer, std::size_t) {
    std::memcpy(buffer, bytes.data(), bytes.size());
    return static_cast<ssize_t>(bytes.size());
  };
}

// Expects every one of the four probed nodes (item 10 of #61) not to open,
// as most tests below need no camera attached when the watch starts.
// Issued while an InSequence is active in the caller, so it chains with
// whatever the caller declares around it.
void NoneAttachedAtStartup(MockV4l2Hotplug& v4l2) {
  EXPECT_CALL(v4l2, Open(StrEq("/dev/video0"), _)).WillOnce(Return(-1));
  EXPECT_CALL(v4l2, Open(StrEq("/dev/video1"), _)).WillOnce(Return(-1));
  EXPECT_CALL(v4l2, Open(StrEq("/dev/video2"), _)).WillOnce(Return(-1));
  EXPECT_CALL(v4l2, Open(StrEq("/dev/video3"), _)).WillOnce(Return(-1));
}

class V4l2HotplugTest : public ::testing::Test {
 protected:
  void SetUp() override { mock = &mock_; }

  void TearDown() override {
    monitor_.reset();
    mock = nullptr;
  }

  // Starts a fresh monitor_, recording every callback into
  // connected_/disconnected_.
  void Start() {
    monitor_ = std::make_unique<V4l2InotifyMonitor>();
    monitor_->Start(
        [this](std::string id) {
          const std::scoped_lock lock(mutex_);
          disconnected_.push_back(std::move(id));
          changed_.notify_all();
        },
        [this](std::string id) {
          const std::scoped_lock lock(mutex_);
          connected_.push_back(std::move(id));
          changed_.notify_all();
        });
  }

  // Stops monitor_ and waits for its thread to join.
  void Stop() { monitor_.reset(); }

  bool WaitForConnected(std::size_t count) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, kPatience,
                             [&] { return connected_.size() >= count; });
  }

  bool WaitForDisconnected(std::size_t count) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, kPatience,
                             [&] { return disconnected_.size() >= count; });
  }

  // Wraps a Read action so WaitForReads can block until it has run,
  // without caring whether it also fires a callback: joining the monitor
  // afterward (Stop) then waits for it to finish handling whatever that
  // read delivered, before a test checks that nothing fired (#111).
  template <typename Action>
  auto CountedRead(Action action) {
    return [this, action](int fd, void* buffer, std::size_t size) {
      const ssize_t result = action(fd, buffer, size);
      const std::scoped_lock lock(mutex_);
      ++reads_;
      changed_.notify_all();
      return result;
    };
  }

  bool WaitForReads(std::size_t count) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, kPatience, [&] { return reads_ >= count; });
  }

  ::testing::StrictMock<MockV4l2Hotplug> mock_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<std::string> connected_;
  std::vector<std::string> disconnected_;
  std::size_t reads_ = 0;
  std::unique_ptr<V4l2InotifyMonitor> monitor_;
};

TEST_F(V4l2HotplugTest, InitFailureStopsWatchingWithoutProbingAnyNode) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(-1));

  Start();
  Stop();
}

TEST_F(V4l2HotplugTest, AddWatchFailureClosesAndStopsWatchingWithoutProbing) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(kNotifyFd, _, _)).WillOnce(Return(-1));

  Start();
  Stop();
}

TEST_F(V4l2HotplugTest, InitialScanProbesAllFourNodesWithoutFiringAnyCallback) {
  EXPECT_CALL(mock_, InotifyInit1(IN_NONBLOCK | IN_CLOEXEC))
      .WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_,
              InotifyAddWatch(kNotifyFd, StrEq("/dev"), IN_CREATE | IN_DELETE))
      .WillOnce(Return(0));
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(StrEq("/dev/video0"), _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video1"), _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video2"), _)).WillOnce(Return(kFd + 1));
    EXPECT_CALL(mock_, QueryCap(kFd + 1, _))
        .WillOnce(Capability(kKnownCard, kKnownBusInfo));
    EXPECT_CALL(mock_, Close(kFd + 1)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video3"), _)).WillOnce(Return(-1));
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);

  Start();
  Stop();

  EXPECT_THAT(connected_, IsEmpty());
  EXPECT_THAT(disconnected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, ANewCameraFiresOnConnected) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    NoneAttachedAtStartup(mock_);
    EXPECT_CALL(mock_, Open(StrEq("/dev/video0"), _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _))
        .WillOnce(Capability("New Camera", "usb-new"));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(Delivers(EncodedEvent(IN_CREATE, "video0")));

  Start();
  ASSERT_TRUE(WaitForConnected(1));
  Stop();

  EXPECT_THAT(connected_, ElementsAre("usb-new"));
  EXPECT_THAT(disconnected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, ACameraThatDoesNotQueryIsIgnoredOnConnect) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    NoneAttachedAtStartup(mock_);
    EXPECT_CALL(mock_, Open(StrEq("/dev/video1"), _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(CountedRead(Delivers(EncodedEvent(IN_CREATE, "video1"))));

  Start();
  ASSERT_TRUE(WaitForReads(1));
  Stop();

  EXPECT_THAT(connected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, AKnownCameraDisconnects) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(StrEq("/dev/video0"), _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video1"), _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video2"), _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _))
        .WillOnce(Capability(kKnownCard, kKnownBusInfo));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video3"), _)).WillOnce(Return(-1));
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(Delivers(EncodedEvent(IN_DELETE, "video2")));

  Start();
  ASSERT_TRUE(WaitForDisconnected(1));
  Stop();

  EXPECT_THAT(disconnected_, ElementsAre(kKnownBusInfo));
  EXPECT_THAT(connected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, AnUnknownNodeDisconnectingIsIgnored) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    NoneAttachedAtStartup(mock_);
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(CountedRead(Delivers(EncodedEvent(IN_DELETE, "video0"))));

  Start();
  ASSERT_TRUE(WaitForReads(1));
  Stop();

  EXPECT_THAT(disconnected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, AnUnrelatedNameIsIgnored) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    NoneAttachedAtStartup(mock_);
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(CountedRead(Delivers(EncodedEvent(IN_CREATE, "null"))));

  Start();
  ASSERT_TRUE(WaitForReads(1));
  Stop();

  EXPECT_THAT(connected_, IsEmpty());
  EXPECT_THAT(disconnected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, AnEventWithNeitherCreateNorDeleteIsIgnored) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    NoneAttachedAtStartup(mock_);
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(CountedRead(Delivers(EncodedEvent(IN_ATTRIB, "video0"))));

  Start();
  ASSERT_TRUE(WaitForReads(1));
  Stop();

  EXPECT_THAT(connected_, IsEmpty());
  EXPECT_THAT(disconnected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, SeveralEventsInOneReadAreAllHandled) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    EXPECT_CALL(mock_, Open(StrEq("/dev/video0"), _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video1"), _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video2"), _)).WillOnce(Return(kFd));
    EXPECT_CALL(mock_, QueryCap(kFd, _))
        .WillOnce(Capability(kKnownCard, kKnownBusInfo));
    EXPECT_CALL(mock_, Close(kFd)).WillOnce(Return(0));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video3"), _)).WillOnce(Return(-1));
    EXPECT_CALL(mock_, Open(StrEq("/dev/video0"), _)).WillOnce(Return(kFd + 1));
    EXPECT_CALL(mock_, QueryCap(kFd + 1, _))
        .WillOnce(Capability("New Camera", "usb-new"));
    EXPECT_CALL(mock_, Close(kFd + 1)).WillOnce(Return(0));
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1)).RetiresOnSaturation();
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(Delivers(EncodedEvents({EncodedEvent(IN_CREATE, "video0"),
                                        EncodedEvent(IN_DELETE, "video2")})));

  Start();
  ASSERT_TRUE(WaitForConnected(1));
  ASSERT_TRUE(WaitForDisconnected(1));
  Stop();

  EXPECT_THAT(connected_, ElementsAre("usb-new"));
  EXPECT_THAT(disconnected_, ElementsAre(kKnownBusInfo));
}

TEST_F(V4l2HotplugTest, AReadFailureEndsWatchingCleanly) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    NoneAttachedAtStartup(mock_);
  }
  EXPECT_CALL(mock_, Select(_, _)).WillOnce(Return(1));
  EXPECT_CALL(mock_, Read(kNotifyFd, _, _))
      .WillOnce(CountedRead(
          [](int, void*, std::size_t) { return static_cast<ssize_t>(-1); }));

  Start();
  ASSERT_TRUE(WaitForReads(1));
  Stop();

  EXPECT_THAT(connected_, IsEmpty());
  EXPECT_THAT(disconnected_, IsEmpty());
}

TEST_F(V4l2HotplugTest, StoppingEndsTheWatchCleanly) {
  EXPECT_CALL(mock_, InotifyInit1(_)).WillOnce(Return(kNotifyFd));
  EXPECT_CALL(mock_, InotifyAddWatch(_, _, _)).WillOnce(Return(0));
  {
    InSequence order;
    NoneAttachedAtStartup(mock_);
  }
  EXPECT_CALL(mock_, Select(_, _)).Times(AnyNumber()).WillRepeatedly(TimesOut);

  Start();
  Stop();

  EXPECT_THAT(connected_, IsEmpty());
  EXPECT_THAT(disconnected_, IsEmpty());
}

}  // namespace
}  // namespace facepalm

// The link-time seam: libv4l2 and inotify, forwarding to the mock.
int v4l2_open(const char* file, int oflag, ...) {
  return facepalm::mock->Open(file, oflag);
}

int v4l2_close(int fd) { return facepalm::mock->Close(fd); }

// libv4l2's own signature; only VIDIOC_QUERYCAP is ever asked for here, so
// `request` itself goes unused.
// NOLINTNEXTLINE(google-runtime-int,bugprone-easily-swappable-parameters)
int v4l2_ioctl(int fd, unsigned long int request, ...) {
  static_cast<void>(request);
  std::va_list arguments;
  va_start(arguments, request);
  void* argument = va_arg(arguments, void*);
  va_end(arguments);
  return facepalm::mock->QueryCap(fd, static_cast<v4l2_capability*>(argument));
}

int inotify_init1(int flags) { return facepalm::mock->InotifyInit1(flags); }

int inotify_add_watch(int fd, const char* path, std::uint32_t inotify_mask) {
  return facepalm::mock->InotifyAddWatch(fd, path, inotify_mask);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int select(int nfds, fd_set* /*readfds*/, fd_set* /*writefds*/,
           fd_set* /*exceptfds*/, timeval* timeout) {
  return facepalm::mock->Select(nfds, timeout);
}

ssize_t read(int fd, void* buffer, std::size_t count) {
  return facepalm::mock->Read(fd, buffer, count);
}
