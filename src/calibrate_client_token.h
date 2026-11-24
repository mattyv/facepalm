#ifndef FACEPALM_SRC_CALIBRATE_CLIENT_TOKEN_H_
#define FACEPALM_SRC_CALIBRATE_CLIENT_TOKEN_H_

#include <optional>
#include <string>

#include "src/client_approvals.h"

namespace facepalm {

// What opening the calibration wizard decided (#118, item 12 of #61,
// #64): either the "calibrate" client's token, to hand CalibrationFlow,
// or an error message already worded for the dialog
// tools/menu/main.mm's -calibrate: and main_linux.cc's OpenCalibration
// each show.
struct CalibrateClientToken {
  std::optional<std::string> token;
  std::string error;  // set only when token is nullopt
};

// Approves the "calibrate" client the same way calibrate_cli itself does
// (ClientApprovals::Approve, through `approvals`), then loads the token
// Approve just wrote (through `load`, ClientApprovals::Load's own
// signature) -- the sequence tools/menu/main.mm's -calibrate: and
// main_linux.cc's OpenCalibration each repeated by hand around their own
// dialog box. A load that fails leaves `token` set to an empty string,
// not an error, matching both platforms' own prior behaviour.
CalibrateClientToken ResolveCalibrateToken(ClientApprovals& approvals,
                                           const ClientApprovals::Load& load);

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATE_CLIENT_TOKEN_H_
