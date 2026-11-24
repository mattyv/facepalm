#ifndef FACEPALM_SRC_DAEMON_ENGINE_SUPPORT_H_
#define FACEPALM_SRC_DAEMON_ENGINE_SUPPORT_H_

#include <memory>
#include <optional>
#include <string>

#include "src/calibration.h"
#include "src/config.h"
#include "src/user_paths.h"

namespace facepalm {

// `daemon_paths->*member`, or empty when `daemon_paths` is itself nullopt
// (#61 item 12, #114, #116, #134): main.cc's own --home resolved to no
// recognized directory, so every one of UserPaths' optional-feature paths
// (preview_request/frame/readings, calibration) reads this way, leaving
// that feature simply off rather than failing the daemon outright.
std::string PathOrEmpty(const std::optional<UserPaths>& daemon_paths,
                        std::string UserPaths::* member);

// CalibrationSession's own tunable thresholds and gains, from Config
// (#137): pulled out of tools/daemon/main.cc's Engine constructor so its
// own cognitive complexity (#64) stays under the project's threshold.
CalibrationSettings CalibrationSettingsFromConfig(const Config& config);

// A fresh copy of `*profile`, shared ownership, or nullptr with no profile
// given (#61 item 12): Engine's own, so a session finishing later can
// replace it wholesale (#136, #137, #167) rather than only borrowing
// main()'s local for the Engine's whole lifetime, or mutating it in place
// while a reader on another thread reads its own snapshot of it.
std::shared_ptr<const CalibrationProfile> SharedCalibrationProfile(
    const CalibrationProfile* profile);

}  // namespace facepalm

#endif  // FACEPALM_SRC_DAEMON_ENGINE_SUPPORT_H_
