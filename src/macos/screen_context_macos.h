#ifndef FACEPALM_SRC_MACOS_SCREEN_CONTEXT_MACOS_H_
#define FACEPALM_SRC_MACOS_SCREEN_CONTEXT_MACOS_H_

#include <optional>
#include <vector>

#include "src/screen_source.h"

namespace facepalm {

// AppKit alone (#61 item 11): NSWorkspace for the frontmost app, NSEvent
// for the mouse, NSScreen for the screens, and CGWindowListCopyWindowInfo
// for a window title. The title needs the Screen Recording permission and
// reads nullopt without it; reading only the frontmost app's name and the
// mouse needs no permission. No permission prompt fires from any of this:
// on macOS, only an API that captures the screen's pixels does, which
// nothing here calls.
//
// It needs a real desktop session, so, following
// src/macos/avfoundation_session's precedent, it has no unit tests or
// coverage floor; ScreenContextReporter, which is tested, takes it behind
// ScreenSource.
class MacosScreenSource : public ScreenSource {
 public:
  std::optional<ActiveApp> CurrentApp(bool include_title) override;
  std::optional<MousePosition> Mouse() override;
  std::vector<ScreenRect> Screens() override;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_MACOS_SCREEN_CONTEXT_MACOS_H_
