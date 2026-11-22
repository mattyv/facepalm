#include "src/look_away_replay.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace facepalm {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Lt;

// A stretch of head_pose lines: `seconds` of them at 15 a second, with the
// head at (`yaw`, `pitch`).
struct Stretch {
  double seconds = 0;
  double yaw = 0;
  double pitch = 0;
};

// Writes `stretch` from `start_ms`, and returns the timestamp after it.
std::int64_t Poses(std::ostream& out, std::int64_t start_ms,
                   const Stretch& stretch) {
  std::int64_t now = start_ms;
  const int frames = static_cast<int>(stretch.seconds * 15);
  for (int frame = 0; frame < frames; ++frame) {
    out << "head_pose " << now << ' ' << stretch.yaw << ' ' << stretch.pitch
        << " 0\n";
    now += 66;
  }
  return now;
}

std::string Replay(const std::string& log, bool* ok = nullptr) {
  std::istringstream input(log);
  std::ostringstream output;
  const bool replayed = ReplayLookingAway(input, output);
  if (ok != nullptr) {
    *ok = replayed;
  }
  return output.str();
}

TEST(LookAwayReplayTest, ReportsALookAwayOnceItReturnsWithItsKindAndDuration) {
  std::ostringstream log;
  std::int64_t now = Poses(log, 1000, {.seconds = 5});
  now = Poses(log, now, {.seconds = 3, .yaw = 30});
  Poses(log, now, {.seconds = 3});
  bool ok = false;
  const std::string output = Replay(log.str(), &ok);
  EXPECT_TRUE(ok);
  // 1,000 + 75 * 66 is the first frame away (5,950); the first frame back
  // (8,920) is 2,970 ms later, under Config's default averted_ms.
  EXPECT_EQ(output, "looked_away 8920 averted 2970\n");
}

TEST(LookAwayReplayTest, IgnoresOtherLines) {
  std::ostringstream log;
  std::int64_t now = Poses(log, 1000, {.seconds = 5});
  log << "gesture_raw 1 2 3\nreaction {}\n";
  Poses(log, now, {.seconds = 0.5, .yaw = 30});
  // Never returns to rest, so there is no look to end either way.
  EXPECT_EQ(Replay(log.str()), "");
}

// #141's kGone: gesture_no_face, like LookAwayReader::OnNoFace, has no
// timestamp of its own; the look it opens is measured from the last
// head_pose line read, to the first once a face returns.
TEST(LookAwayReplayTest, GestureNoFaceReportsGoneOnceAFaceReturns) {
  std::ostringstream log;
  Poses(log, 1000, {.seconds = 5});
  log << "gesture_no_face\n";
  // The gap itself, in real time: the fixture format carries none for a
  // gesture_no_face line, so the test writes the next head_pose line far
  // enough ahead of the last one to stand for it.
  Poses(log, 8000, {.seconds = 1});
  EXPECT_EQ(Replay(log.str()), "looked_away 8000 gone 2116\n");
}

// A no-face stretch too short to hold is a glance, as any other is.
TEST(LookAwayReplayTest, ABriefGestureNoFaceIsAGlance) {
  std::ostringstream log;
  Poses(log, 1000, {.seconds = 5});
  log << "gesture_no_face\n";
  Poses(log, 5900, {.seconds = 1});
  EXPECT_EQ(Replay(log.str()), "");
}

TEST(LookAwayReplayTest, RejectsAMalformedOrOutOfOrderLine) {
  bool ok = true;
  Replay("head_pose 10 1 2\n", &ok);
  EXPECT_FALSE(ok);
  Replay("head_pose 10 1 2 3 4\n", &ok);
  EXPECT_FALSE(ok);
  Replay("head_pose 10 1 2 3\nhead_pose 10 1 2 3\n", &ok);
  EXPECT_FALSE(ok);
}

TEST(LookAwayReplayTest, FailsWhenTheInputCannotBeRead) {
  std::istringstream input("head_pose 10 1 2 3\n");
  input.setstate(std::ios::badbit);
  std::ostringstream output;
  EXPECT_FALSE(ReplayLookingAway(input, output));
}

TEST(LookAwayReplayTest, FailsWhenTheOutputCannotBeWritten) {
  std::ostringstream log;
  std::int64_t now = Poses(log, 1000, {.seconds = 5});
  Poses(log, now, {.seconds = 3, .yaw = 30});
  std::istringstream input(log.str());
  std::ostringstream output;
  output.setstate(std::ios::badbit);
  EXPECT_FALSE(ReplayLookingAway(input, output));
}

// Recorded head poses from facepalm's own probe, numbers only (#35); see
// test/fixtures/README.md for where each comes from.
std::string Fixture(const std::string& name) {
  const std::ifstream file(std::string(FACEPALM_FIXTURES) + "/" + name);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

// The seconds into `log` at which each look away was reported (its own
// return, not its start: #141).
std::vector<double> ReportSeconds(const std::string& log) {
  std::int64_t first = 0;
  std::istringstream(log.substr(log.find(' ') + 1)) >> first;
  std::vector<double> seconds;
  std::istringstream output(Replay(log));
  std::string gesture;
  std::int64_t timestamp = 0;
  std::string reported_kind;
  std::int64_t duration_ms = 0;
  while (output >> gesture >> timestamp >> reported_kind >> duration_ms) {
    seconds.push_back(static_cast<double>(timestamp - first) / 1000);
  }
  return seconds;
}

// Prompted: down at 6 s, center at 12, down at 18, center at 24. Each look
// down is reported once, a little after the head returns to center (#141:
// the report is the look's end, not when hold_ms elapses while still
// away).
TEST(LookAwayReplayTest, EachRecordedLookDownIsReportedOnce) {
  EXPECT_THAT(ReportSeconds(Fixture("look_down.head_pose")),
              ElementsAre(AllOf(Gt(12), Lt(15)), AllOf(Gt(24), Lt(27))));
}

// Prompted: down at 6 s, center at 13, a slight turn at 17, center at 24,
// a medium turn at 29, center at 35, a wide turn at 40, center at 47. The
// two turns the face model kept tracking are each reported once, a little
// after the head returns to center; during the wide one it lost the face
// (no frames from 41 to 46 s) for long enough that its own resting pose
// relearns before the head is read as back, so there is nothing to
// return from and nothing is reported.
TEST(LookAwayReplayTest, EachRecordedTurnAwayIsReportedOnce) {
  EXPECT_THAT(ReportSeconds(Fixture("turns.head_pose")),
              ElementsAre(AllOf(Gt(24), Lt(27)), AllOf(Gt(35), Lt(38))));
}

// Prompted: a frown at 12 s, neutral at 22, a smile while nodding at 28,
// finish at 38. The nods reach 12 degrees from the resting pose.
TEST(LookAwayReplayTest, RecordedNodsAreNotReported) {
  EXPECT_THAT(ReportSeconds(Fixture("nods.head_pose")), IsEmpty());
}

}  // namespace
}  // namespace facepalm
