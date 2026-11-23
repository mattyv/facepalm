#include "src/macos/screen_context_macos.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <optional>
#include <string>
#include <vector>

#include "src/screen_context.h"
#include "src/screen_source.h"

namespace facepalm {
namespace {

// `app`'s window title, from the on-screen window list, or nullopt
// without one: a window the app has not shown yet, or the kCGWindowName
// key macOS withholds without the Screen Recording permission.
std::optional<std::string> TitleOf(NSRunningApplication* app) {
  const pid_t pid = app.processIdentifier;
  const CFArrayRef windows = CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (windows == nullptr) {
    return std::nullopt;
  }
  std::optional<std::string> title;
  for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
    NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(windows, i);
    NSNumber* owner_pid = info[(__bridge NSString*)kCGWindowOwnerPID];
    NSNumber* layer = info[(__bridge NSString*)kCGWindowLayer];
    if (owner_pid.intValue != pid || layer.intValue != 0) {
      continue;
    }
    NSString* name = info[(__bridge NSString*)kCGWindowName];
    if (name != nil && name.length > 0) {
      title = std::string(name.UTF8String);
    }
    break;
  }
  CFRelease(windows);
  return title;
}

}  // namespace

std::optional<ActiveApp> MacosScreenSource::CurrentApp(bool include_title) {
  NSRunningApplication* app = [[NSWorkspace sharedWorkspace] frontmostApplication];
  if (app == nil || app.localizedName == nil) {
    return std::nullopt;
  }
  ActiveApp result{.name = std::string(app.localizedName.UTF8String), .title = std::nullopt};
  if (include_title) {
    result.title = TitleOf(app);
  }
  return result;
}

namespace {

// [NSScreen screens].firstObject (#168, #169): the screen with the menu
// bar, stable regardless of which window has focus, unlike
// [NSScreen mainScreen] -- the key window's own screen -- which this
// source used to read instead and which swaps as focus moves between
// monitors, unrelated to which one the camera faces.
NSScreen* PrimaryScreen() { return [NSScreen screens].firstObject; }

double PrimaryScreenHeight(NSScreen* primary) {
  return primary != nil ? primary.frame.size.height : 0;
}

}  // namespace

std::optional<MousePosition> MacosScreenSource::Mouse() {
  const NSPoint point = [NSEvent mouseLocation];
  const double primary_height = PrimaryScreenHeight(PrimaryScreen());
  return MousePosition{.x = point.x, .y = YDownFromYUp(point.y, /*height=*/0, primary_height)};
}

std::vector<ScreenRect> MacosScreenSource::Screens() {
  std::vector<ScreenRect> screens;
  NSScreen* const primary = PrimaryScreen();
  const double primary_height = PrimaryScreenHeight(primary);
  for (NSScreen* screen in [NSScreen screens]) {
    const NSRect frame = screen.frame;
    screens.push_back({.x = frame.origin.x,
                       .y = YDownFromYUp(frame.origin.y, frame.size.height, primary_height),
                       .width = frame.size.width,
                       .height = frame.size.height,
                       .main = (screen == primary)});
  }
  return screens;
}

}  // namespace facepalm
