#include "src/head_pose_reader.h"

#include <cstdint>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/reaction.h"

namespace facepalm {

void HeadPoseReader::OnFace(const Face& face, int /*width*/, int /*height*/,
                            std::int64_t /*timestamp_ms*/,
                            std::vector<Reaction>& reactions) {
  reactions.push_back(ReactionFrom(face));
}

void HeadPoseReader::OnNoFace() {}

}  // namespace facepalm
