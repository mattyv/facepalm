#ifndef FACEPALM_SRC_MACOS_PREVIEW_JPEG_H_
#define FACEPALM_SRC_MACOS_PREVIEW_JPEG_H_

#include <optional>
#include <string>

#include "src/frame.h"

namespace facepalm {

// Encodes `frame` as a JPEG, through ImageIO (as tools/camera_probe.cc's
// PngSink encodes a PNG), at `quality` (0 to 1, ImageIO's own scale).
// Nothing if ImageIO could not encode it. The only caller is the camera
// preview (#37 item 8): the bytes go to UserPaths::preview_frame, never
// to any client the approved-token service reaches.
std::optional<std::string> EncodePreviewJpeg(const Frame& frame, float quality);

}  // namespace facepalm

#endif  // FACEPALM_SRC_MACOS_PREVIEW_JPEG_H_
