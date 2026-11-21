#ifndef FACEPALM_SRC_LINUX_SCREEN_CONTEXT_LINUX_H_
#define FACEPALM_SRC_LINUX_SCREEN_CONTEXT_LINUX_H_

#include <optional>
#include <vector>

#include "src/screen_source.h"

namespace facepalm {

// X11 alone, reached through XWayland where a Wayland session runs one
// (#61 item 11): XQueryPointer for the mouse, _NET_ACTIVE_WINDOW and
// WM_CLASS/_NET_WM_NAME for the active app and its title, and RandR's
// monitors for the screens.
//
// Verified limits, not assumed: under a GNOME Wayland session, Mutter
// keeps a Wayland client's real pointer position and window focus from
// X11 clients (a 2014 GNOME report gives the reason: X11's global
// coordinates are never handed to a client, to keep Wayland's security
// model), and XWayland sets _NET_ACTIVE_WINDOW to None once a native
// Wayland window has focus (reported fixing Wine's own XWayland
// integration). So, rather than report a stale or partial answer, the
// mouse and the active app always read nullopt when WAYLAND_DISPLAY is
// set, whether or not an XWayland socket answers; RandR's monitor
// geometry is a separate, more reliable subsystem XWayland still forwards
// correctly, and is read either way. With no X server reachable at all
// (XOpenDisplay fails), every call reads empty or nullopt.
//
// It needs a real desktop session, so, following
// src/macos/avfoundation_session's precedent, it has no unit tests or
// coverage floor; ScreenContextReporter, which is tested, takes it behind
// ScreenSource.
class LinuxScreenSource : public ScreenSource {
 public:
  LinuxScreenSource();
  ~LinuxScreenSource() override;
  LinuxScreenSource(const LinuxScreenSource&) = delete;
  LinuxScreenSource& operator=(const LinuxScreenSource&) = delete;

  std::optional<ActiveApp> CurrentApp(bool include_title) override;
  std::optional<MousePosition> Mouse() override;
  std::vector<ScreenRect> Screens() override;

 private:
  // An X11 Display*, or nullptr with no server reachable; kept as void* so
  // Xlib's macros (None, Status, True, False and the rest) do not leak
  // into every file that includes this header.
  void* display_;
  bool wayland_session_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LINUX_SCREEN_CONTEXT_LINUX_H_
