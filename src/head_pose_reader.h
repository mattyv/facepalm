#ifndef FACEPALM_SRC_HEAD_POSE_READER_H_
#define FACEPALM_SRC_HEAD_POSE_READER_H_

#include <cstdint>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/face_reader.h"

namespace facepalm {

// Reacts to each face with its head's yaw, pitch and roll (#21). Replaces
// HeadPoseAnalyzer's own logic, now that FaceAnalyzer runs the face model
// for every reader (#34).
class HeadPoseReader : public FaceReader {
 public:
  void OnFace(const Face& face, int width, int height,
              std::int64_t timestamp_ms,
              std::vector<Reaction>& reactions) override;
  void OnNoFace() override;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_HEAD_POSE_READER_H_
