#include "src/settings_config_path.h"

#include <string>
#include <string_view>

namespace facepalm {

std::string SettingsConfigPath(std::string_view xdg_config_home,
                               std::string_view home) {
  std::string base = xdg_config_home.empty() ? std::string(home) + "/.config"
                                             : std::string(xdg_config_home);
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  return base + "/facepalm/facepalm.conf";
}

}  // namespace facepalm
