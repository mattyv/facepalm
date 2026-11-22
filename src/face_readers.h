#ifndef FACEPALM_SRC_FACE_READERS_H_
#define FACEPALM_SRC_FACE_READERS_H_

#include <memory>
#include <vector>

#include "src/calibration.h"
#include "src/config.h"
#include "src/face_reader.h"
#include "src/screen_context_reader.h"

namespace facepalm {

// The readers the daemon gives the face model's result on every frame, in
// order: head gestures, facial expressions, looking away, screen context
// and calibration. The first three run only when the config enables them,
// with the config's settings. `screen_context_reader`, when given and
// `config.screen_context_enabled`, is wrapped so it keeps being fed,
// rather than owned here: it outlives any one call, the way the daemon's
// long-lived pose cache has to (#61 item 11), so the caller owns it and
// may pass the same one again next time. `calibration`, when given, is
// always added (#116): calibration has no config flag of its own,
// gated only by whether the daemon's composition root resolved a profile
// store for this run (src/daemon_calibration.h).
std::vector<std::unique_ptr<FaceReader>> MakeFaceReaders(
    const Config& config, ScreenContextReader* screen_context_reader = nullptr,
    CalibrationController* calibration = nullptr);

}  // namespace facepalm

#endif  // FACEPALM_SRC_FACE_READERS_H_
