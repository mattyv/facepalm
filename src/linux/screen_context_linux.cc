#include "src/linux/screen_context_linux.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "src/screen_source.h"

namespace facepalm {
namespace {

// The active window `_NET_ACTIVE_WINDOW` names, or nullopt without one: no
// EWMH window manager, or none focused.
std::optional<Window> ActiveWindowOf(Display* display) {
  const Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
  if (active == None) {
    return std::nullopt;
  }
  Atom type = None;
  int format = 0;
  unsigned long count = 0;
  unsigned long after = 0;
  unsigned char* data = nullptr;
  const int status = XGetWindowProperty(display, DefaultRootWindow(display),
                                        active, 0, 1, False, XA_WINDOW, &type,
                                        &format, &count, &after, &data);
  if (status != Success || data == nullptr) {
    return std::nullopt;
  }
  Window window = None;
  if (count >= 1) {
    window = *reinterpret_cast<Window*>(data);
  }
  XFree(data);
  return window == None ? std::nullopt : std::make_optional(window);
}

// `window`'s WM_CLASS, read as the app's name (there is no bundle
// identifier on Linux to match macOS's).
std::optional<std::string> AppNameOf(Display* display, Window window) {
  XClassHint hint{};
  if (XGetClassHint(display, window, &hint) == 0) {
    return std::nullopt;
  }
  std::optional<std::string> name;
  if (hint.res_class != nullptr && hint.res_class[0] != '\0') {
    name = std::string(hint.res_class);
  }
  if (hint.res_name != nullptr) {
    XFree(hint.res_name);
  }
  if (hint.res_class != nullptr) {
    XFree(hint.res_class);
  }
  return name;
}

// `window`'s _NET_WM_NAME, or, without one, its ICCCM WM_NAME.
std::optional<std::string> TitleOf(Display* display, Window window) {
  const Atom net_name = XInternAtom(display, "_NET_WM_NAME", True);
  if (net_name != None) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    constexpr long kLongestTitleWords = 1024;
    const int status = XGetWindowProperty(
        display, window, net_name, 0, kLongestTitleWords, False,
        AnyPropertyType, &type, &format, &count, &after, &data);
    if (status == Success && data != nullptr && count > 0) {
      std::string title(reinterpret_cast<char*>(data), count);
      XFree(data);
      if (!title.empty()) {
        return title;
      }
    } else if (data != nullptr) {
      XFree(data);
    }
  }
  char* name = nullptr;
  if (XFetchName(display, window, &name) != 0 && name != nullptr) {
    const std::string title(name);
    XFree(name);
    if (!title.empty()) {
      return title;
    }
  }
  return std::nullopt;
}

}  // namespace

LinuxScreenSource::LinuxScreenSource()
    : display_(XOpenDisplay(nullptr)),
      wayland_session_(std::getenv("WAYLAND_DISPLAY") != nullptr) {}

LinuxScreenSource::~LinuxScreenSource() {
  if (display_ != nullptr) {
    XCloseDisplay(static_cast<Display*>(display_));
  }
}

std::optional<ActiveApp> LinuxScreenSource::CurrentApp(bool include_title) {
  // Verified limit (see the header): a Wayland session's real focus is
  // kept from X11 clients, so this is never read through XWayland.
  if (wayland_session_ || display_ == nullptr) {
    return std::nullopt;
  }
  auto* display = static_cast<Display*>(display_);
  const std::optional<Window> window = ActiveWindowOf(display);
  if (!window) {
    return std::nullopt;
  }
  const std::optional<std::string> name = AppNameOf(display, *window);
  if (!name) {
    return std::nullopt;
  }
  ActiveApp result{.name = *name, .title = std::nullopt};
  if (include_title) {
    result.title = TitleOf(display, *window);
  }
  return result;
}

std::optional<MousePosition> LinuxScreenSource::Mouse() {
  // Verified limit (see the header): a Wayland session's real pointer
  // position is kept from X11 clients, so this is never read through
  // XWayland.
  if (wayland_session_ || display_ == nullptr) {
    return std::nullopt;
  }
  auto* display = static_cast<Display*>(display_);
  Window root_return = None;
  Window child_return = None;
  int root_x = 0;
  int root_y = 0;
  int window_x = 0;
  int window_y = 0;
  unsigned int mask = 0;
  // Xlib always fills root_x/root_y; its Bool result says only whether the
  // pointer is on the same (legacy, pre-RandR) X11 screen as the root
  // window asked for, not whether the call succeeded.
  XQueryPointer(display, DefaultRootWindow(display), &root_return,
                &child_return, &root_x, &root_y, &window_x, &window_y, &mask);
  return MousePosition{.x = static_cast<double>(root_x),
                       .y = static_cast<double>(root_y)};
}

std::vector<ScreenRect> LinuxScreenSource::Screens() {
  std::vector<ScreenRect> screens;
  if (display_ == nullptr) {
    return screens;
  }
  auto* display = static_cast<Display*>(display_);
  int count = 0;
  XRRMonitorInfo* monitors =
      XRRGetMonitors(display, DefaultRootWindow(display), True, &count);
  if (monitors == nullptr) {
    return screens;
  }
  screens.reserve(count);
  for (int i = 0; i < count; ++i) {
    screens.push_back({.x = static_cast<double>(monitors[i].x),
                       .y = static_cast<double>(monitors[i].y),
                       .width = static_cast<double>(monitors[i].width),
                       .height = static_cast<double>(monitors[i].height),
                       .main = monitors[i].primary != 0});
  }
  XRRFreeMonitors(monitors);
  return screens;
}

}  // namespace facepalm
