#ifndef FACEPALM_SRC_PREVIEW_PUBLISHER_H_
#define FACEPALM_SRC_PREVIEW_PUBLISHER_H_

#include <functional>
#include <optional>
#include <string>

#include "src/frame.h"
#include "src/preview_lease.h"

namespace facepalm {

// Turns a frame into JPEG bytes, or nothing if it cannot (#37 item 8).
// src/macos/preview_jpeg.h is the only real one.
using PreviewEncoder = std::function<std::optional<std::string>(const Frame&)>;

// Drives PreviewLease from the menu's own request file and, while it
// grants streaming, encodes and publishes each frame (#37 item 8, #36).
// No image reaches any client but the menu through this: `publish`
// writes only to the private per-user file the menu polls
// (UserPaths::preview_frame), never to the approved-token service every
// other client reaches.
class PreviewPublisher {
 public:
  // `requested` reports whether the menu is currently asking for the
  // preview (UserPaths::preview_request's presence); `encode` and
  // `publish` are given a frame's JPEG bytes and where to put them.
  PreviewPublisher(std::function<bool()> requested, PreviewEncoder encode,
                   std::function<bool(const std::string&)> publish,
                   PreviewLease& lease);

  // Call once for each frame the camera captures, from the same queue
  // that already dispatches them.
  void OnFrame(const Frame& frame);

 private:
  std::function<bool()> requested_;
  PreviewEncoder encode_;
  std::function<bool(const std::string&)> publish_;
  PreviewLease& lease_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_PREVIEW_PUBLISHER_H_
