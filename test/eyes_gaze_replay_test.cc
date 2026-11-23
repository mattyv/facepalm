#include "src/eyes_gaze_replay.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace facepalm {
namespace {

using ::testing::Each;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::Not;

// Writes one paired head_pose/eyes frame, eyes_probe.h's own blendshape
// order: eyeLookDown, In, Out, Up (each left then right), eyeBlink,
// eyeSquint, eyeWide (each left then right), then six landmarks' x, y, z
// (unused here, so zeroed).
struct EyeScores {
  double look_down_left = 0;
  double look_down_right = 0;
  double look_in_left = 0;
  double look_in_right = 0;
  double look_out_left = 0;
  double look_out_right = 0;
  double look_up_left = 0;
  double look_up_right = 0;
  double blink_left = 0;
  double blink_right = 0;
  double squint_left = 0;
  double squint_right = 0;
  double wide_left = 0;
  double wide_right = 0;
};

void WriteFrame(std::ostream& out, std::int64_t timestamp_ms, double yaw,
                double pitch, const EyeScores& eyes) {
  out << "head_pose " << timestamp_ms << ' ' << yaw << ' ' << pitch << " 0\n";
  out << "eyes " << timestamp_ms << ' ' << eyes.look_down_left << ' '
      << eyes.look_down_right << ' ' << eyes.look_in_left << ' '
      << eyes.look_in_right << ' ' << eyes.look_out_left << ' '
      << eyes.look_out_right << ' ' << eyes.look_up_left << ' '
      << eyes.look_up_right << ' ' << eyes.blink_left << ' ' << eyes.blink_right
      << ' ' << eyes.squint_left << ' ' << eyes.squint_right << ' '
      << eyes.wide_left << ' ' << eyes.wide_right
      << " 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
}

std::string Replay(const std::string& log, bool* ok = nullptr) {
  std::istringstream input(log);
  std::ostringstream output;
  const bool replayed = ReplayEyesAndLookAway(input, output);
  if (ok != nullptr) {
    *ok = replayed;
  }
  return output.str();
}

TEST(EyesGazeReplayTest, RejectsAnEyesLineWithNoMatchingHeadPose) {
  std::ostringstream log;
  log << "eyes 1000 0 0 0 0 0 0 0 0 0 0 0 0 0 0";
  for (int i = 0; i < 18; ++i) {
    log << " 0";
  }
  log << '\n';
  bool ok = true;
  Replay(log.str(), &ok);
  EXPECT_FALSE(ok);
}

TEST(EyesGazeReplayTest, RejectsAMismatchedTimestamp) {
  std::ostringstream log;
  WriteFrame(log, 1000, 0, 0, {});
  std::string text = log.str();
  // Change the eyes line's own timestamp so it no longer matches.
  const std::size_t eyes_at = text.find("\neyes 1000");
  text.replace(eyes_at + 1, std::string("eyes 1000").size(), "eyes 1001");
  bool ok = true;
  Replay(text, &ok);
  EXPECT_FALSE(ok);
}

TEST(EyesGazeReplayTest, RejectsAMalformedHeadPoseLine) {
  bool ok = true;
  Replay("head_pose 10 1 2\n", &ok);
  EXPECT_FALSE(ok);
}

TEST(EyesGazeReplayTest, RejectsAHeadPoseLineWithTrailingGarbage) {
  bool ok = true;
  Replay("head_pose 10 1 2 3 4\n", &ok);
  EXPECT_FALSE(ok);
}

TEST(EyesGazeReplayTest, RejectsAHeadPoseTimestampThatDoesNotIncrease) {
  std::ostringstream log;
  WriteFrame(log, 1000, 0, 0, {});
  // A second head_pose at the same timestamp as the frame already replayed,
  // with no eyes line needed to catch it: `last` already holds 1000.
  log << "head_pose 1000 0 0 0\n";
  bool ok = true;
  Replay(log.str(), &ok);
  EXPECT_FALSE(ok);
}

TEST(EyesGazeReplayTest, RejectsAMalformedEyesLine) {
  std::ostringstream log;
  log << "head_pose 1000 0 0 0\n";
  log << "eyes 1000 0 0\n";  // Too few of FormatEyesLine's own 32 numbers.
  bool ok = true;
  Replay(log.str(), &ok);
  EXPECT_FALSE(ok);
}

TEST(EyesGazeReplayTest, FailsWhenTheInputCannotBeRead) {
  std::istringstream input("head_pose 10 1 2 3\n");
  input.setstate(std::ios::badbit);
  std::ostringstream output;
  EXPECT_FALSE(ReplayEyesAndLookAway(input, output));
}

TEST(EyesGazeReplayTest, FailsWhenTheOutputCannotBeWritten) {
  std::ostringstream log;
  WriteFrame(log, 1000, 0, 0, {});
  std::istringstream input(log.str());
  std::ostringstream output;
  output.setstate(std::ios::badbit);
  EXPECT_FALSE(ReplayEyesAndLookAway(input, output));
}

TEST(EyesGazeReplayTest, PrintsComposedGazeForEachPairedFrame) {
  std::ostringstream log;
  for (int i = 0; i < 50; ++i) {
    WriteFrame(log, 1000 + (i * 66), 0, 0, {});
  }
  const std::string output = Replay(log.str());
  EXPECT_THAT(output, ::testing::HasSubstr("gaze 1000 "));
}

// Recorded fixtures from #139 (test/fixtures/eyes/README.md): four cues,
// six seconds apart, 15 fps, one screen.
std::string Fixture(const std::string& name) {
  const std::ifstream file(std::string(FACEPALM_FIXTURES) + "/eyes/" + name);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

struct GazeSample {
  std::int64_t timestamp_ms = 0;
  double yaw_degrees = 0;
  double pitch_degrees = 0;
};

std::vector<GazeSample> GazeSamples(const std::string& output) {
  std::vector<GazeSample> samples;
  std::istringstream in(output);
  std::string kind;
  while (in >> kind) {
    if (kind == "gaze") {
      GazeSample sample;
      in >> sample.timestamp_ms >> sample.yaw_degrees >> sample.pitch_degrees;
      samples.push_back(sample);
    } else {
      std::string rest;
      std::getline(in, rest);
    }
  }
  return samples;
}

// `output` and `prefix` are never swapped in practice: every call site
// passes a replay's own output first, a literal line-kind prefix second.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<std::string> LinesStartingWith(const std::string& output,
                                           const std::string& prefix) {
  std::vector<std::string> lines;
  std::istringstream in(output);
  std::string line;
  while (std::getline(in, line)) {
    if (line.starts_with(prefix)) {
      lines.push_back(line);
    }
  }
  return lines;
}

// The samples whose own timestamp falls `seconds_from_start` or later into
// the replay, up to the next full second (one second of samples, at
// 15 fps about 15 of them), for a steady-state reading well clear of a
// cue's own transition.
std::vector<GazeSample> Window(const std::vector<GazeSample>& samples,
                               double seconds_from_start) {
  if (samples.empty()) {
    return {};
  }
  const std::int64_t start =
      samples.front().timestamp_ms +
      static_cast<std::int64_t>(seconds_from_start * 1000);
  std::vector<GazeSample> window;
  for (const GazeSample& sample : samples) {
    if (sample.timestamp_ms >= start && sample.timestamp_ms < start + 1000) {
      window.push_back(sample);
    }
  }
  return window;
}

double MeanYaw(const std::vector<GazeSample>& samples) {
  double sum = 0;
  for (const GazeSample& sample : samples) {
    sum += sample.yaw_degrees;
  }
  return samples.empty() ? 0 : sum / static_cast<double>(samples.size());
}

// Cues: center; look at the top-left corner, head still; center; look at
// the bottom-right corner, head still (test/fixtures/eyes/README.md).
// Eyes alone, with the head not turning, still moves the composed gaze:
// the corner cues (4.5 s in, well past the transition) read a clearly
// different yaw than the center cues either side of them.
TEST(EyesGazeReplayTest, EyesOnlyCornerLookMovesComposedGaze) {
  const std::vector<GazeSample> samples =
      GazeSamples(Replay(Fixture("eyes_to_corners.probe")));
  ASSERT_THAT(samples, Not(IsEmpty()));
  const double center = MeanYaw(Window(samples, 4.5));
  const double top_left = MeanYaw(Window(samples, 10.5));
  const double bottom_right = MeanYaw(Window(samples, 22.5));
  EXPECT_GT(top_left - center, 3.0);
  EXPECT_LT(bottom_right - center, -3.0);
}

// Cues: center; turn head left, eyes on the camera; center; turn head
// right, eyes on the camera. The eyes compensate to keep looking at the
// camera as the head turns, so the composed gaze -- unlike the head pose
// alone, which swings past 29 degrees -- stays much closer to center.
TEST(EyesGazeReplayTest, HeadTurnWithEyesCenteredKeepsGazeNearCentre) {
  const std::vector<GazeSample> samples =
      GazeSamples(Replay(Fixture("head_turn_eyes_centered.probe")));
  ASSERT_THAT(samples, Not(IsEmpty()));
  const double turned_left = MeanYaw(Window(samples, 10.5));
  const double turned_right = MeanYaw(Window(samples, 22.5));
  EXPECT_THAT(std::abs(turned_left), Le(15.0));
  EXPECT_THAT(std::abs(turned_right), Le(15.0));
}

// Cues: look at the screen; glance down at the keyboard; look at the
// screen again; glance down once more and hold it. Neither glance holds
// the composed gaze away from its resting pose for long enough, or far
// enough, to be reported at all.
TEST(EyesGazeReplayTest, GlanceAtTheKeyboardIsNotReportedAsALookAway) {
  const std::string output = Replay(Fixture("glance_keyboard.probe"));
  EXPECT_THAT(LinesStartingWith(output, "looked_away"), IsEmpty());
}

// Cues: look at the camera, as if just asked a question; look away to
// think; look back with the answer; look away again, for a harder
// question. Each pause away is reported once it returns, as averted
// (back within looking_away.averted_ms), not away.
TEST(EyesGazeReplayTest, ThinkingAversionReportsAverted) {
  const std::string output = Replay(Fixture("thinking_aversion.probe"));
  const std::vector<std::string> reports =
      LinesStartingWith(output, "looked_away");
  ASSERT_THAT(reports, Not(IsEmpty()));
  for (const std::string& report : reports) {
    EXPECT_THAT(report, ::testing::HasSubstr(" averted "));
  }
}

// Cues: look at the camera; turn away completely; come back; turn away
// completely again and stay away. The composed gaze reads well past
// looking_away.degrees during the held turns, and the recording ends
// with the face lost -- gone, in #141's own terms -- confirming the
// tuned pipeline reads this fixture as genuinely away throughout, not
// just at its own edges.
//
// Neither turn away is asserted as a *reported* looked_away line: the
// first one's own face is reacquired well off the resting pose (a
// gesture_no_face run of 52 frames, about 3.47 s), and facepalm#157
// (found tuning this fixture) is that LookAwayDetector can lose a
// report's whole look across a gap that long; the second turn never
// returns within the recording at all, so it has nothing to report
// either way.
TEST(EyesGazeReplayTest, FullyAwayGazeReadsAwayAndEndsWithNoFace) {
  const std::string output = Replay(Fixture("fully_away.probe"));
  const std::vector<GazeSample> samples = GazeSamples(output);
  ASSERT_THAT(samples, Not(IsEmpty()));
  const double resting_yaw = MeanYaw(Window(samples, 0.5));
  bool saw_away = false;
  for (const GazeSample& sample : samples) {
    if (std::abs(sample.yaw_degrees - resting_yaw) > 15.0) {
      saw_away = true;
    }
  }
  EXPECT_TRUE(saw_away);
  EXPECT_THAT(LinesStartingWith(output, "no_face"), Not(IsEmpty()));
}

// Cues: look ahead normally; blink a few times, as while reading; rest;
// blink a few more times. A blink lifts eyeSquint the same way a real
// squint does (#140), and the squint-guard (expressions.eye_look_down_
// threshold, expressions.eye_blink_threshold, plus #158's own settle
// time bridging eyeSquint's slower decay between two close blinks) holds
// eyes_narrowed back throughout; EyeGazeTracker skips blink frames
// outright, so the composed gaze never swings on one either way.
TEST(EyesGazeReplayTest, BlinksAreNotReadAsEyesNarrowedAndDontMoveGaze) {
  const std::string output = Replay(Fixture("blinks.probe"));
  const std::vector<std::string> narrowed =
      LinesStartingWith(output, "expression");
  int eyes_narrowed_count = 0;
  for (const std::string& line : narrowed) {
    if (line.find("eyes_narrowed") != std::string::npos) {
      ++eyes_narrowed_count;
    }
  }
  EXPECT_EQ(eyes_narrowed_count, 0) << "facepalm#158";
  const std::vector<GazeSample> samples = GazeSamples(output);
  ASSERT_THAT(samples, Not(IsEmpty()));
  const double resting_yaw = MeanYaw(Window(samples, 0.5));
  for (const GazeSample& sample : samples) {
    EXPECT_THAT(std::abs(sample.yaw_degrees - resting_yaw), Le(10.0));
  }
}

// Cues: look ahead normally; close the eyes slowly, hold, then open;
// rest; close again, hold longer, then open. The same two guarantees as
// blinks.probe, for a much longer blink.
TEST(EyesGazeReplayTest, LongBlinksLikewise) {
  const std::string output = Replay(Fixture("long_blinks.probe"));
  EXPECT_THAT(LinesStartingWith(output, "expression"),
              Each(Not(::testing::HasSubstr("eyes_narrowed"))));
  const std::vector<GazeSample> samples = GazeSamples(output);
  ASSERT_THAT(samples, Not(IsEmpty()));
  const double resting_yaw = MeanYaw(Window(samples, 0.5));
  for (const GazeSample& sample : samples) {
    EXPECT_THAT(std::abs(sample.yaw_degrees - resting_yaw), Le(10.0));
  }
}

}  // namespace
}  // namespace facepalm
