#include "src/screen_label.h"

#include <string>
#include <string_view>

namespace facepalm {
namespace {

std::string_view Trim(std::string_view value) {
  constexpr std::string_view kWhitespace = " \t\n\r\f\v";
  const auto first = value.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  return value.substr(first, value.find_last_not_of(kWhitespace) - first + 1);
}

}  // namespace

std::string ScreenLabel(const ScreenLabelInfo& info) {
  const std::string_view model = Trim(info.model);
  const std::string_view manufacturer = Trim(info.manufacturer);
  std::string name;
  if (!model.empty()) {
    if (!manufacturer.empty() && !model.starts_with(manufacturer)) {
      name = std::string(manufacturer) + " ";
    }
    name += model;
  } else if (info.connector.starts_with("eDP") ||
             info.connector.starts_with("LVDS")) {
    name = "built-in display";
  } else if (info.connector.starts_with("DP") ||
             info.connector.starts_with("HDMI") ||
             info.connector.starts_with("VGA") ||
             info.connector.starts_with("DVI")) {
    name = "external display";
  } else {
    name = "display";
  }
  return std::string(info.position) + " (" + name + ")";
}

}  // namespace facepalm
