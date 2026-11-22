#ifndef FACEPALM_SRC_LINUX_PREVIEW_JPEG_H_
#define FACEPALM_SRC_LINUX_PREVIEW_JPEG_H_

#include <optional>
#include <string>

#include "src/frame.h"

namespace facepalm {

// Encodes `frame` as a JPEG, through libjpeg (already a dependency,
// libv4l-dev's own sibling on every distribution that packages it,
// needing no new one), at `quality` (0 to 1, clamped to libjpeg's 1-100
// scale, as src/macos/preview_jpeg.h's ImageIO-backed sibling clamps
// its own out-of-range input rather than failing on it). Nothing if
// libjpeg could not encode it. The only caller is the camera preview
// (#37 item 8, #108): the bytes go to UserPaths::preview_frame, never
// to any client the approved-token service reaches.
std::optional<std::string> EncodePreviewJpeg(const Frame& frame, float quality);

}  // namespace facepalm

#endif  // FACEPALM_SRC_LINUX_PREVIEW_JPEG_H_
