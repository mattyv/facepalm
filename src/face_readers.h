#ifndef FACEPALM_SRC_FACE_READERS_H_
#define FACEPALM_SRC_FACE_READERS_H_

#include <memory>
#include <vector>

#include "src/config.h"
#include "src/face_reader.h"

namespace facepalm {

// The readers the daemon gives the face model's result on every frame, in
// order: head gestures, facial expressions and looking away, each when the
// config enables it, with the config's settings.
std::vector<std::unique_ptr<FaceReader>> MakeFaceReaders(const Config& config);

}  // namespace facepalm

#endif  // FACEPALM_SRC_FACE_READERS_H_
