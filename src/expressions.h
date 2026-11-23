#ifndef FACEPALM_SRC_EXPRESSIONS_H_
#define FACEPALM_SRC_EXPRESSIONS_H_

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
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
//
// eyes_narrowed alone has a second gate (#140): a downward glance, as at a
// keyboard, or a blink lifts eyeSquint the same way a real squint does, so
// while MediaPipe's own eyeLookDown or eyeBlink score is above its
// threshold, eyes_narrowed neither reports nor learns, though the other
// four expressions are read as usual. This is read from the raw
// blendshapes directly, never from #137's composed gaze, so it holds
// whether or not that reading is available. The gate also stays shut for
// kEyeGateSettleMs after eyeLookDown and eyeBlink themselves last crossed
// back under their threshold (#158): eyeSquint's own decay lags theirs,
// so between two blinks close together -- a rapid blink, not a single
// glance or a deliberate blink -- it can still read as elevated once
// they have already cleared; the settle bridges that gap without
// touching Settings::hold_ms, which every other expression shares.
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
    // eyes_narrowed's own gate (#140): the eyeLookDown score, either eye,
    // above which a downward glance holds the squint back.
    double eye_look_down_threshold;
    // eyes_narrowed's own gate (#140): the eyeBlink score, either eye,
    // above which a blink holds the squint back.
    double eye_blink_threshold;
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
  // When it reopens on a resting pose more than Settings::pose_degrees
  // from the one it shut on, the posture itself changed and the scores
  // shifted with it, so the resting face is learned afresh, as after a
  // gap; after a nod or a glance the head returns to the pose it left, and
  // the levels it froze still stand.
  //
  // eyes_narrowed has the second gate above it (#140): while `scores`'
  // eyeLookDown or eyeBlink, either eye, is above its own threshold, that
  // one state neither reports nor learns from this frame, though the
  // other four expressions still do; its own hold restarts once the
  // glance or blink ends, the same way a hold that straddles the pose
  // gate restarts.
  std::vector<Expression> Update(const Scores& scores, const HeadRotation& pose,
                                 std::int64_t timestamp_ms);

  // The resting pose as of the last Update, which LookAwayDetector judges
  // looking away against (#35): nothing before the first frame, nor while
  // the resting face and pose are still being learned, in the first three
  // seconds and again after a gap or a change of posture.
  [[nodiscard]] std::optional<HeadRotation> RestingPose() const;

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

  // Tracks the gate's shut and reopen transitions from `away`, and returns
  // whether it just reopened on a resting pose more than
  // Settings::pose_degrees from the one it shut on: the posture changed
  // while the gate was shut, so the frozen levels no longer describe the
  // face the scores now come from, and the resting face must be learned
  // afresh.
  bool PostureChangedWhileGated(bool away);

  // Whether `scores`' eyeLookDown or eyeBlink, either eye, is above its
  // threshold: eyes_narrowed's own gate (#140), read from MediaPipe's raw
  // blendshapes, not #137's composed gaze.
  [[nodiscard]] bool EyesGated(const Scores& scores) const;

  // Whether the squint gate (EyesGated) should hold shut on this frame:
  // `scores` reads it directly, or it is still within kEyeGateSettleMs of
  // the last frame that did (#158). Updates eyes_gated_ms_, the last such
  // frame's own timestamp.
  bool EyesSettling(const Scores& scores, std::int64_t timestamp_ms);

  Settings settings_;
  std::array<double, 3> resting_pose_{};
  std::deque<std::pair<std::int64_t, std::array<double, 3>>> recent_poses_;
  std::int64_t moved_ms_ = kNotRisen;
  bool gated_ = false;  // Whether the gate was shut on the last frame.
  std::array<double, 3> gated_pose_{};      // The resting pose when it shut.
  std::int64_t eyes_gated_ms_ = kNotRisen;  // EyesSettling's own settle time.
  std::array<State, kExpressions> states_{};
  bool started_ = false;
  std::int64_t started_ms_ = 0;
  std::int64_t last_ms_ = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_EXPRESSIONS_H_
