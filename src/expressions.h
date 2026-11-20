#ifndef FACEPALM_SRC_EXPRESSIONS_H_
#define FACEPALM_SRC_EXPRESSIONS_H_

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

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
class ExpressionTracker {
 public:
  // Their defaults live in Config.
  struct Settings {
    // The smallest rise above the resting level that counts.
    double rise;
    // How many times the resting spread a rise must also exceed.
    double spreads;
    // How long a rise must last, so that a blink is not a squint.
    std::int64_t hold_ms;
  };

  explicit ExpressionTracker(Settings settings);

  // Takes the next frame's scores and returns the expressions that started
  // on it, in Expression's order. `timestamp_ms` must increase from call to
  // call. The first three seconds, and the first three after a gap of more
  // than two, only learn the resting face.
  std::vector<Expression> Update(const Scores& scores,
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

  // Starts learning the resting face from `scores`.
  void StartOver(const Scores& scores, std::int64_t timestamp_ms);

  Settings settings_;
  std::array<State, kExpressions> states_{};
  bool started_ = false;
  std::int64_t started_ms_ = 0;
  std::int64_t last_ms_ = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_EXPRESSIONS_H_
