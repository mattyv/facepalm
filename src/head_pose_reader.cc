#include "src/head_pose_reader.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/reaction.h"

namespace facepalm {

void HeadPoseReader::OnFace(const Face& face, int /*width*/, int /*height*/,
                            std::int64_t /*timestamp_ms*/,
                            std::vector<Reaction>& reactions) {
  // A degenerate pose gives no angles, so no reaction (#44).
  if (std::optional<Reaction> reaction = ReactionFrom(face)) {
    reactions.push_back(*std::move(reaction));
  }
}

void HeadPoseReader::OnNoFace() {}

}  // namespace facepalm
