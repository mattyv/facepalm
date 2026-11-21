#ifndef FACEPALM_SRC_LOOK_AWAY_READER_H_
#define FACEPALM_SRC_LOOK_AWAY_READER_H_

#include <cstdint>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"
#include "src/face_reader.h"
#include "src/look_away.h"

namespace facepalm {

// Reports looking away (#35) as {"gesture":"looking_away"}: each face's head
// pose goes to LookAwayDetector with the resting pose an ExpressionTracker
// learns from the same faces. The tracker is configured as the expression
// reader's is, so its resting pose is the one ExpressionReader learns, and
// it keeps working when expressions are turned off.
class LookAwayReader : public FaceReader {
 public:
  LookAwayReader(ExpressionTracker::Settings tracker,
                 LookAwayDetector::Settings detector);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // Neither keeps a window to drop: the tracker learns the resting pose
  // again on its own after a gap of more than two seconds, and the detector
  // then has no resting pose until it is learned.
  void OnNoFace() override;

 private:
  ExpressionTracker tracker_;
  LookAwayDetector detector_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_LOOK_AWAY_READER_H_
