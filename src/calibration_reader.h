#ifndef FACEPALM_SRC_CALIBRATION_READER_H_
#define FACEPALM_SRC_CALIBRATION_READER_H_

#include <cstdint>
#include <vector>

#include "src/calibration.h"
#include "src/camera_controller.h"
#include "src/eye_gaze.h"
#include "src/face_model.h"
#include "src/face_reader.h"

namespace facepalm {

// Feeds a calibration session its poses and eye signal (#36, #35, item 12
// of #61, #137): each face's head pose and EyeSignalFrom's own reading go
// to CalibrationController::Update, which ignores them unless a session
// is running. It reports no reaction of its own; the session's own
// protocol replies carry its steps.
class CalibrationReader : public FaceReader {
 public:
  // `blink_threshold` is EyeSignalFrom's own (Config's eye_gaze.blink_
  // threshold).
  CalibrationReader(CalibrationController& controller, double blink_threshold);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // No window to drop: a session only uses the pose this frame.
  void OnNoFace() override;

 private:
  CalibrationController& controller_;
  double blink_threshold_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATION_READER_H_
