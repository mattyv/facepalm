#ifndef FACEPALM_SRC_SETTINGS_CONFIG_PATH_H_
#define FACEPALM_SRC_SETTINGS_CONFIG_PATH_H_

#include <string>
#include <string_view>

namespace facepalm {

// Where facepalm.conf lives (#34, #64): under `xdg_config_home` when it is
// not empty, else under `home`'s own ~/.config -- tools/menu/main.mm's
// SettingsURL and main_linux.cc's ConfigPath each resolved this the same
// way by hand, wrapped in their own platform's path type (NSURL, QDir).
std::string SettingsConfigPath(std::string_view xdg_config_home,
                               std::string_view home);

}  // namespace facepalm

#endif  // FACEPALM_SRC_SETTINGS_CONFIG_PATH_H_
