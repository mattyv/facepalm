#ifndef FACEPALM_SRC_CALIBRATION_READER_H_
#define FACEPALM_SRC_CALIBRATION_READER_H_

#include <cstdint>
#include <vector>

#include "src/calibration.h"
#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/face_reader.h"

namespace facepalm {

// Feeds a calibration session its poses (#36, #35, item 12 of #61): each
// face's head pose goes to CalibrationController::Update, which ignores it
// unless a session is running. It reports no reaction of its own; the
// session's own protocol replies carry its steps.
class CalibrationReader : public FaceReader {
 public:
  explicit CalibrationReader(CalibrationController& controller);

  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  // No window to drop: a session only uses the pose this frame.
  void OnNoFace() override;

 private:
  CalibrationController& controller_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_CALIBRATION_READER_H_
