#ifndef FACEPALM_SRC_SCREEN_LABEL_H_
#define FACEPALM_SRC_SCREEN_LABEL_H_

#include <string>
#include <string_view>

namespace facepalm {

struct ScreenLabelInfo {
  std::string_view manufacturer;
  std::string_view model;
  std::string_view connector;
  std::string_view position;
};

// Built-in displays use a generic label; other displays use their model when
// available. Connector names classify displays and are never presented.
std::string ScreenLabel(const ScreenLabelInfo& info);

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_LABEL_H_
