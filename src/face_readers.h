#ifndef FACEPALM_SRC_FACE_READERS_H_
#define FACEPALM_SRC_FACE_READERS_H_

#include <memory>
#include <vector>

#include "src/config.h"
#include "src/face_reader.h"
#include "src/screen_context_reader.h"

namespace facepalm {

// The readers the daemon gives the face model's result on every frame, in
// order: head gestures, facial expressions, looking away and screen
// context, each when the config enables it, with the config's settings.
// `screen_context_reader`, when given and `config.screen_context_enabled`,
// is wrapped so it keeps being fed, rather than owned here: it outlives any
// one call, the way the daemon's long-lived pose cache has to (#61 item
// 11), so the caller owns it and may pass the same one again next time.
std::vector<std::unique_ptr<FaceReader>> MakeFaceReaders(
    const Config& config, ScreenContextReader* screen_context_reader = nullptr);

}  // namespace facepalm

#endif  // FACEPALM_SRC_FACE_READERS_H_
