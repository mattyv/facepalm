#include "src/expression_reader.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "src/camera_controller.h"
#include "src/expressions.h"
#include "src/face_model.h"

namespace facepalm {
namespace {

// The reaction for `expression`, named by Name(): {"expression":"smile"}.
Reaction ReactionFor(Expression expression) {
  return std::string(R"({"expression":")") + std::string(Name(expression)) +
         "\"}";
}

}  // namespace

ExpressionReader::ExpressionReader(ExpressionTracker::Settings settings)
    : tracker_(settings) {}

void ExpressionReader::OnFace(const Face& face, int /*width*/, int /*height*/,
                              std::int64_t timestamp_ms,
                              std::vector<Reaction>& reactions) {
  Scores scores{};
  std::ranges::copy(face.blendshapes, scores.begin());
  for (const Expression started : tracker_.Update(scores, timestamp_ms)) {
    reactions.push_back(ReactionFor(started));
  }
}

void ExpressionReader::OnNoFace() {}

}  // namespace facepalm
