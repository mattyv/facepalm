#ifndef FACEPALM_SRC_GESTURE_READER_H_
#define FACEPALM_SRC_GESTURE_READER_H_

#include <cstdint>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/head_gesture_model.h"

namespace facepalm {

// Detects head gestures with Idiap's CCDbHG model (#34), replacing
// GestureAnalyzer and Nodding Pigeon: each face goes into a window of the
// last second, resampled to the model's training rate; the model runs each
// time the window is ready. Reports a gesture once when it starts, not
// again until the head is next still or a different gesture starts
// (5ad22cb's rule, carried over from Nodding Pigeon).
class GestureReader : public FaceReader {
 public:
  explicit GestureReader(float confidence_threshold);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  void OnNoFace() override;

 private:
  float confidence_threshold_;
  head_gesture::Window window_;
  // The last gesture reported, or kNone since the head was last still.
  head_gesture::Gesture reported_ = head_gesture::Gesture::kNone;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_GESTURE_READER_H_
