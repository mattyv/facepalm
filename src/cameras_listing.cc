#include "src/cameras_listing.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace facepalm {
namespace {

constexpr std::size_t kFields = 3;

// The fields of `line`, split at each tab.
std::vector<std::string_view> Fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  for (std::size_t tab = line.find('\t'); tab != std::string_view::npos;
       tab = line.find('\t', start)) {
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  fields.push_back(line.substr(start));
  return fields;
}

}  // namespace

std::vector<CamerasListingEntry> ParseCamerasListing(std::string_view text) {
  std::vector<CamerasListingEntry> devices;
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    const std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view()
                                         : text.substr(end + 1);
    const std::vector<std::string_view> fields = Fields(line);
    if (fields.size() == kFields) {
      devices.push_back({.id = std::string(fields[0]),
                         .name = std::string(fields[1]),
                         .built_in = fields[2] == "1"});
    }
  }
  return devices;
}

std::string FormatCamerasListing(
    const std::vector<CamerasListingEntry>& cameras) {
  std::string text;
  for (const CamerasListingEntry& camera : cameras) {
    text += camera.id;
    text += '\t';
    text += camera.name;
    text += '\t';
    text += camera.built_in ? '1' : '0';
    text += '\n';
  }
  return text;
}

}  // namespace facepalm
