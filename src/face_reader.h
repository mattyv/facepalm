#ifndef FACEPALM_SRC_FACE_READER_H_
#define FACEPALM_SRC_FACE_READER_H_

#include <cstdint>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"

namespace facepalm {

// Something FaceAnalyzer gives the face model's result on every frame, such
// as the head-pose reaction or the head-gesture model (#34): the face model
// now runs once a frame, for every reader, rather than each analyser
// loading its own.
class FaceReader {
 public:
  virtual ~FaceReader() = default;

  // The face the model found this frame, and the frame's pixel size (the
  // head-gesture model's landmark geometry needs it; a reader that does not
  // need it can ignore the two parameters). May append to `reactions`.
  virtual void OnFace(const Face& face, int width, int height,
                      std::int64_t timestamp_ms,
                      std::vector<Reaction>& reactions) = 0;

  // No face this frame: a reader keeping a window of its own should drop
  // it.
  virtual void OnNoFace() = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FACE_READER_H_
