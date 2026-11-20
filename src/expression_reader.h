#ifndef FACEPALM_SRC_EXPRESSION_READER_H_
#define FACEPALM_SRC_EXPRESSION_READER_H_

#include <cstdint>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/face_reader.h"

namespace facepalm {

// Detects facial expressions with ExpressionTracker (#46): each face's
// blendshape scores and timestamp go to the tracker, and each expression it
// reports as started becomes one reaction, {"expression":"smile"} and so
// on, named by Name().
class ExpressionReader : public FaceReader {
 public:
  explicit ExpressionReader(ExpressionTracker::Settings settings);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // The tracker keeps no window of its own to drop here: it starts learning
  // the resting face again on its own, once the gap since the last frame it
  // saw passes two seconds (ExpressionTracker::Update).
  void OnNoFace() override;

 private:
  ExpressionTracker tracker_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_EXPRESSION_READER_H_
