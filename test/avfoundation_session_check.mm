// Tests src/macos/avfoundation_session.mm's device-change observers through
// real NSNotificationCenter posts, following preview_jpeg_check.mm's own
// pattern of a real-framework check rather than facepalm_unit's mocked,
// coverage-floored kind: AvFoundationSession has no seam to mock
// NSNotificationCenter behind, and none is needed here, since posting a
// notification needs no camera, open or otherwise (#121). Each observer is
// added with `queue:nil`, so its block runs synchronously on the thread that
// posts, with no wait needed.

#include "src/macos/avfoundation_session.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "src/frame.h"

namespace facepalm {
namespace {

// #121: constructing an AvFoundationSession must not itself watch for a
// device connecting or disconnecting, which could then call back into a
// sibling (such as Engine's camera_ in tools/daemon/main.cc) before its own
// constructor runs.
TEST(AvFoundationSessionTest, ConstructingDoesNotWatchDevicesUntilStartWatchingIsCalled) {
  int disconnected = 0;
  int connected = 0;
  const AvFoundationSession session(
      /*device_id=*/"", [](const Frame&) {}, [] {}, [&](const std::string&) { ++disconnected; },
      [&](const std::string&) { ++connected; });

  [NSNotificationCenter.defaultCenter
      postNotificationName:AVCaptureDeviceWasDisconnectedNotification
                    object:nil];
  [NSNotificationCenter.defaultCenter postNotificationName:AVCaptureDeviceWasConnectedNotification
                                                    object:nil];

  EXPECT_EQ(disconnected, 0);
  EXPECT_EQ(connected, 0);
}

// StartWatching registers the observers with exactly the callbacks the
// constructor was given (#121), the way V4l2Session::StartWatching wires
// HotplugMonitor to the Linux analog of the same two callbacks.
TEST(AvFoundationSessionTest, StartWatchingWiresDeviceChangeCallbacksToTheGivenOnes) {
  std::optional<std::string> disconnected;
  std::optional<std::string> connected;
  AvFoundationSession session(
      /*device_id=*/"", [](const Frame&) {}, [] {},
      [&](const std::string& id) { disconnected = id; },
      [&](const std::string& id) { connected = id; });

  session.StartWatching();
  [NSNotificationCenter.defaultCenter
      postNotificationName:AVCaptureDeviceWasDisconnectedNotification
                    object:nil];
  [NSNotificationCenter.defaultCenter postNotificationName:AVCaptureDeviceWasConnectedNotification
                                                    object:nil];

  // `object:nil` carries no uniqueID; UniqueId's own nil branch turns that
  // into an empty string, the same empty id a real notification would carry
  // if the device's uniqueID were ever nil.
  EXPECT_EQ(disconnected, std::optional<std::string>(""));
  EXPECT_EQ(connected, std::optional<std::string>(""));
}

}  // namespace
}  // namespace facepalm
