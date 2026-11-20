#ifndef FACEPALM_SRC_EXPRESSIONS_H_
#define FACEPALM_SRC_EXPRESSIONS_H_

#include <array>
#include <cstdint>
#include <deque>
#include <string_view>
#include <utility>
#include <vector>

#include "src/head_pose.h"

namespace facepalm {

// MediaPipe's 52 blendshape scores for one face, in its order, each 0 to 1.
inline constexpr int kBlendshapeScores = 52;
using Scores = std::array<float, kBlendshapeScores>;

// A facial expression facepalm reports (#46). A frown shows as kBrowsDown,
// and a squint as kEyesNarrowed, often with kBrowsDown too: a squint lowers
// the brows as far as a small frown does.
enum class Expression : std::uint8_t {
  kBrowsUp,
  kSmile,
  kLipsPressed,
  kBrowsDown,
  kEyesNarrowed,
};

// The expression's name in a reaction: brows_up, smile, lips_pressed,
// brows_down or eyes_narrowed.
std::string_view Name(Expression expression);

// Finds where expressions start in a stream of one face's scores.
//
// Each expression is read from a few scores, and judged against the
// person's own resting face: the level each score sits at, and how far it
// wobbles around it, both learned as frames arrive. That follows OpenFace's
// person-specific calibration of its action units, which subtracts a
// running estimate of the neutral face (Baltrusaitis et al., FG 2015); the
// method only, none of its code.
//
// The scores are also coupled to head pose: a nod or a tilt moves them as
// much as an expression does (#46). While the head is away from its
// resting pose, or has moved more than a little recently, the tracker
// gates the scores out: it neither reports nor learns from them, so a nod
// is never read as an expression.
class ExpressionTracker {
 public:
  // Their defaults live in Config.
  struct Settings {
    // The smallest rise above the resting level that counts.
    double rise;
    // How far any of yaw, pitch or roll may be from the resting pose, in
    // degrees, before the head is read as away.
    double pose_degrees;
    // The largest range any one of yaw, pitch or roll may have had over the
    // last 500 ms before the head is read as moving.
    double pose_range;
    // How long after the head was last away or moving it is still
    // settling, and the gate stays shut.
    std::int64_t settle_ms;
    // How many times the resting spread a rise must also exceed.
    double spreads;
    // How long a rise must last, so that a blink is not a squint.
    std::int64_t hold_ms;
  };

  explicit ExpressionTracker(Settings settings);

  // Takes the next frame's scores and head pose and returns the
  // expressions that started on it, in Expression's order. `timestamp_ms`
  // must increase from call to call. The first three seconds, and the first
  // three after a gap of more than two, only learn the resting face and the
  // resting pose.
  //
  // While the head is away from its resting pose by more than
  // Settings::pose_degrees, or has moved more than Settings::pose_range
  // within the last 500 ms, or did either within the last
  // Settings::settle_ms, the scores follow the pose and not the face: this
  // call reports nothing and neither the levels nor the spreads learn from
  // it, though any expression already showing keeps showing. The resting
  // pose itself keeps learning throughout, including while away, so a new
  // posture held for a while becomes the resting pose and the gate reopens.
  std::vector<Expression> Update(const Scores& scores, const HeadRotation& pose,
                                 std::int64_t timestamp_ms);

 private:
  static constexpr int kExpressions = 5;

  // One expression's resting face and whether it is showing.
  struct State {
    double level = 0;   // Where its scores rest.
    double spread = 0;  // Their mean distance from the level, at rest.
    bool showing = false;
    std::int64_t risen_ms = kNotRisen;  // When its scores last rose.
  };

  static constexpr std::int64_t kNotRisen = -1;

  // Starts learning the resting face and the resting pose from `scores`
  // and `pose`.
  void StartOver(const Scores& scores, const HeadRotation& pose,
                 std::int64_t timestamp_ms);

  // Updates the resting pose from `pose`, learning at `level_weight`, and
  // returns whether the head is away from it or has moved recently (#46):
  // levels and spreads must not learn from this frame when it is.
  bool Away(const HeadRotation& pose, double level_weight, bool learning,
            std::int64_t timestamp_ms);

  Settings settings_;
  std::array<double, 3> resting_pose_{};
  std::deque<std::pair<std::int64_t, std::array<double, 3>>> recent_poses_;
  std::int64_t moved_ms_ = kNotRisen;
  std::array<State, kExpressions> states_{};
  bool started_ = false;
  std::int64_t started_ms_ = 0;
  std::int64_t last_ms_ = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_EXPRESSIONS_H_
