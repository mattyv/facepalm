#include "src/frame_times.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace facepalm {
namespace {

// Reads all of `text` as a whole number, with no sign, space or suffix.
template <typename Number>
std::optional<Number> Whole(std::string_view text) {
  Number value{};
  const auto [last, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.starts_with('-') || error != std::errc() ||
      last != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// The index in `image`'s file name, frame_<index>.png, as camera_probe's
// Recorder names its frames, or nullopt when it is not named so.
std::optional<std::int64_t> FrameIndex(std::string_view image) {
  constexpr std::string_view kPrefix = "frame_";
  constexpr std::string_view kSuffix = ".png";
  const std::size_t slash = image.rfind('/');
  std::string_view name =
      slash == std::string_view::npos ? image : image.substr(slash + 1);
  if (!name.starts_with(kPrefix) || !name.ends_with(kSuffix)) {
    return std::nullopt;
  }
  name.remove_prefix(kPrefix.size());
  name.remove_suffix(kSuffix.size());
  return Whole<std::int64_t>(name);
}

// Reads `manifest`'s lines into `times`, by frame index. Returns false and
// sets `error` at the first line it cannot read or that repeats an index.
bool ReadManifest(std::string_view manifest,
                  std::map<std::int64_t, std::int64_t>& times,
                  std::string& error) {
  int number = 0;
  while (!manifest.empty()) {
    ++number;
    const std::size_t newline = manifest.find('\n');
    const std::string_view line = manifest.substr(0, newline);
    manifest.remove_prefix(newline == std::string_view::npos ? manifest.size()
                                                             : newline + 1);
    const std::size_t space = line.find(' ');
    const std::optional<std::int64_t> index =
        Whole<std::int64_t>(line.substr(0, space));
    const std::optional<std::int64_t> time =
        space == std::string_view::npos
            ? std::nullopt
            : Whole<std::int64_t>(line.substr(space + 1));
    if (!index || !time) {
      error = "timestamps line " + std::to_string(number) +
              " is not \"<index> <milliseconds>\"";
      return false;
    }
    if (!times.emplace(*index, *time).second) {
      error = "timestamps line " + std::to_string(number) + " repeats frame " +
              std::to_string(*index);
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::vector<std::int64_t>> FrameTimes(
    std::string_view manifest, const std::vector<std::string>& images,
    std::string& error) {
  std::map<std::int64_t, std::int64_t> by_index;
  if (!ReadManifest(manifest, by_index, error)) {
    return std::nullopt;
  }
  std::vector<std::int64_t> times;
  for (std::size_t i = 0; i < images.size(); ++i) {
    const std::string& image = images[i];
    const std::optional<std::int64_t> index = FrameIndex(image);
    if (!index) {
      error = image + " is not named frame_<index>.png";
      return std::nullopt;
    }
    const auto found = by_index.find(*index);
    if (found == by_index.end()) {
      error = image + " has no line in the timestamps";
      return std::nullopt;
    }
    if (!times.empty() && found->second <= times.back()) {
      error = image + " was captured at " + std::to_string(found->second) +
              " ms, not after " + images[i - 1] + " at " +
              std::to_string(times.back()) + " ms";
      return std::nullopt;
    }
    times.push_back(found->second);
  }
  return times;
}

}  // namespace facepalm
