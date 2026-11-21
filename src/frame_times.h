#ifndef FACEPALM_SRC_FRAME_TIMES_H_
#define FACEPALM_SRC_FRAME_TIMES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace facepalm {

// Returns the capture time, in milliseconds, of each of `images`, in their
// order, from a recording's `manifest`: camera_probe's timestamps.txt, one
// "<index> <milliseconds>" line per frame, whose index is the number in the
// frame's file name, frame_<index>.png (#42). The times must increase from
// one image to the next, as MediaPipe's VIDEO mode requires. Returns nullopt
// and sets `error` when a line cannot be read, an index repeats, an image is
// not a numbered frame or has no line, or a time does not increase.
std::optional<std::vector<std::int64_t>> FrameTimes(
    std::string_view manifest, const std::vector<std::string>& images,
    std::string& error);

}  // namespace facepalm

#endif  // FACEPALM_SRC_FRAME_TIMES_H_
