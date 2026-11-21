#ifndef FACEPALM_SRC_SCREEN_SOURCE_H_
#define FACEPALM_SRC_SCREEN_SOURCE_H_

#include <optional>
#include <string>
#include <vector>

#include "src/screen_context.h"

namespace facepalm {

// The app the desktop currently has focus on: its name, and, only when
// asked for, its window's title. Nothing else about it is read.
struct ActiveApp {
  std::string name;
  std::optional<std::string> title;
};

// Where the mouse pointer is, in the same coordinate space as Screens().
struct MousePosition {
  double x = 0;
  double y = 0;
};

// Screen context's one OS-facing seam (#61 item 11): one implementation per
// platform (src/macos, src/linux), each built only on its own platform and
// with no unit tests of its own, following src/macos/avfoundation_session's
// precedent (it needs a real desktop session, which a test runner may not
// have, and no permission prompt may fire from a test). Every caller that
// needs to be tested takes this interface and is tested against a mock.
class ScreenSource {
 public:
  virtual ~ScreenSource() = default;

  // The frontmost app, or nullopt when it cannot be read. `include_title`
  // asks for its window's title too (Config's
  // screen_context.window_titles_enabled, off by default): on macOS this
  // needs the Screen Recording permission and is nullopt without it; on
  // Linux it needs an EWMH window manager. Asking without `include_title`
  // needs no extra permission on either platform.
  virtual std::optional<ActiveApp> CurrentApp(bool include_title) = 0;

  // The mouse pointer's position, or nullopt when it cannot be read.
  virtual std::optional<MousePosition> Mouse() = 0;

  // Every screen the desktop knows about, with ScreenRect::main set on the
  // one the camera is assumed to face (#61 item 11 has no calibration to
  // know which screen that really is); empty when the arrangement cannot
  // be read.
  virtual std::vector<ScreenRect> Screens() = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_SOURCE_H_
