#include "src/gesture_reader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "src/camera_controller.h"
#include "src/face_model.h"
#include "src/head_gesture_model.h"

namespace facepalm {
namespace {

using head_gesture::Gesture;

// The reaction for a gesture that just started, indexed by Gesture's own
// order. Idiap's "up/down" reads up_down, so a client need not parse a
// slash out of JSON. kNone is never looked up (OnFace never reports it),
// but holds its place so the array stays indexed by Gesture.
constexpr std::array<std::string_view, head_gesture::kClasses> kReactionJson = {
    "",
    R"({"gesture":"nod"})",
    R"({"gesture":"shake"})",
    R"({"gesture":"tilt"})",
    R"({"gesture":"turn"})",
    R"({"gesture":"up_down"})",
};

std::string_view ReactionFor(Gesture gesture) {
  return kReactionJson.at(static_cast<std::size_t>(gesture));
}

}  // namespace

GestureReader::GestureReader(float confidence_threshold)
    : confidence_threshold_(confidence_threshold) {}

// `width` and `height` follow FaceReader::OnFace's established order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void GestureReader::OnFace(const Face& face, int width, int height,
                           std::int64_t timestamp_ms,
                           std::vector<Reaction>& reactions) {
  const head_gesture::RawFrame raw =
      head_gesture::RawFrameFrom(face, width, height);
  const std::optional<
      std::array<head_gesture::RawFrame, head_gesture::kWindowFrames>>
      window = window_.Add(raw, timestamp_ms);
  if (!window) {
    return;
  }
  const Gesture gesture = head_gesture::Classify(head_gesture::Predict(*window),
                                                 confidence_threshold_);
  // A gesture is reported once, and again only after a still head or the
  // other gesture: the model can flicker to kNone mid-gesture (#34, carried
  // over from Nodding Pigeon's own GestureAnalyzer, 5ad22cb).
  if (gesture == Gesture::kNone) {
    reported_ = gesture;
  } else if (gesture != reported_) {
    reactions.emplace_back(ReactionFor(gesture));
    reported_ = gesture;
  }
}

void GestureReader::OnNoFace() {
  window_.Reset();
  reported_ = Gesture::kNone;
}

}  // namespace facepalm
