#include "src/macos/screen_context_macos.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <optional>
#include <string>
#include <vector>

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

std::optional<MousePosition> MacosScreenSource::Mouse() {
  const NSPoint point = [NSEvent mouseLocation];
  return MousePosition{.x = point.x, .y = point.y};
}

std::vector<ScreenRect> MacosScreenSource::Screens() {
  std::vector<ScreenRect> screens;
  NSScreen* main = [NSScreen mainScreen];
  for (NSScreen* screen in [NSScreen screens]) {
    const NSRect frame = screen.frame;
    screens.push_back({.x = frame.origin.x,
                       .y = frame.origin.y,
                       .width = frame.size.width,
                       .height = frame.size.height,
                       .main = (screen == main)});
  }
  return screens;
}

}  // namespace facepalm
