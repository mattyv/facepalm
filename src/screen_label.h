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

// A display model and its position for calibration prompts. Connector names
// classify fallbacks only; they are not presented as display models.
std::string ScreenLabel(const ScreenLabelInfo& info);

}  // namespace facepalm

#endif  // FACEPALM_SRC_SCREEN_LABEL_H_
