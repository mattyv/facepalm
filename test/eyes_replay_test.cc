#include "src/eyes_replay.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace facepalm {
namespace {

// One well-formed `eyes` line at `timestamp_ms`, with a distinct value on
// every field, the way FormatEyesLine (src/eyes_probe.h) would write one.
std::string Line(std::int64_t timestamp_ms) {
  return "eyes " + std::to_string(timestamp_ms) +
         " 0.11 0.12 0.13 0.14 0.15 0.16 0.17 0.18 0.09 0.10 0.19 0.20 0.21"
         " 0.22"
         " 0.468 0.1 0.01 0.473 0.2 0.02"
         " 0.033 0.3 0.03 0.133 0.4 0.04 0.362 0.5 0.05 0.263 0.6 0.06";
}

TEST(ParseEyesLineTest, ParsesEveryFieldInOrder) {
  const std::optional<EyesFrame> parsed = ParseEyesLine(Line(1234));
  ASSERT_TRUE(parsed.has_value());
  const EyesFrame frame =
      parsed.value();  // NOLINT(bugprone-unchecked-optional-access)
  EXPECT_EQ(frame.timestamp_ms, 1234);
  EXPECT_EQ(frame.eye_look_down, (std::array<double, 2>{0.11, 0.12}));
  EXPECT_EQ(frame.eye_look_in, (std::array<double, 2>{0.13, 0.14}));
  EXPECT_EQ(frame.eye_look_out, (std::array<double, 2>{0.15, 0.16}));
  EXPECT_EQ(frame.eye_look_up, (std::array<double, 2>{0.17, 0.18}));
  EXPECT_EQ(frame.eye_blink, (std::array<double, 2>{0.09, 0.10}));
  EXPECT_EQ(frame.eye_squint, (std::array<double, 2>{0.19, 0.20}));
  EXPECT_EQ(frame.eye_wide, (std::array<double, 2>{0.21, 0.22}));
  EXPECT_EQ(frame.iris_left, (std::array<double, 3>{0.468, 0.1, 0.01}));
  EXPECT_EQ(frame.iris_right, (std::array<double, 3>{0.473, 0.2, 0.02}));
  EXPECT_EQ(frame.corner_33, (std::array<double, 3>{0.033, 0.3, 0.03}));
  EXPECT_EQ(frame.corner_133, (std::array<double, 3>{0.133, 0.4, 0.04}));
  EXPECT_EQ(frame.corner_362, (std::array<double, 3>{0.362, 0.5, 0.05}));
  EXPECT_EQ(frame.corner_263, (std::array<double, 3>{0.263, 0.6, 0.06}));
}

TEST(ParseEyesLineTest, RejectsALineThatIsNotAnEyesLine) {
  EXPECT_FALSE(ParseEyesLine("head_pose 10 1 2 3").has_value());
  EXPECT_FALSE(ParseEyesLine("").has_value());
}

TEST(ParseEyesLineTest, RejectsAnEyesLineMissingAField) {
  const std::string line = Line(1234);
  EXPECT_FALSE(ParseEyesLine(line.substr(0, line.rfind(' '))).has_value());
}

TEST(ParseEyesLineTest, RejectsAnEyesLineMissingAScore) {
  EXPECT_FALSE(ParseEyesLine("eyes 1234 0.11 0.12 0.13").has_value());
}

TEST(ParseEyesLineTest, RejectsAnEyesLineMissingATimestamp) {
  EXPECT_FALSE(ParseEyesLine("eyes").has_value());
}

TEST(ParseEyesLineTest, RejectsAnEyesLineWithATrailingToken) {
  EXPECT_FALSE(ParseEyesLine(Line(1234) + " 0").has_value());
}

TEST(ReplayEyesTest, ReplaysEveryEyesLineInOrder) {
  std::istringstream input(Line(1000) + "\n" + Line(1066) + "\n");
  std::vector<EyesFrame> frames;
  EXPECT_TRUE(ReplayEyes(input, frames));
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].timestamp_ms, 1000);
  EXPECT_EQ(frames[1].timestamp_ms, 1066);
}

TEST(ReplayEyesTest, SkipsEveryOtherKindOfLine) {
  std::istringstream input("head_pose 10 1 2 3\n" + Line(1000) +
                           "\nblendshapes 10 0.1\nreaction {}\n" + Line(1066) +
                           "\ngesture_no_face\n");
  std::vector<EyesFrame> frames;
  EXPECT_TRUE(ReplayEyes(input, frames));
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(frames[0].timestamp_ms, 1000);
  EXPECT_EQ(frames[1].timestamp_ms, 1066);
}

TEST(ReplayEyesTest, RejectsAMalformedEyesLine) {
  std::istringstream input(Line(1000) + " extra\n");
  std::vector<EyesFrame> frames;
  EXPECT_FALSE(ReplayEyes(input, frames));
}

TEST(ReplayEyesTest, RejectsAnOutOfOrderTimestamp) {
  std::istringstream input(Line(1066) + "\n" + Line(1000) + "\n");
  std::vector<EyesFrame> frames;
  EXPECT_FALSE(ReplayEyes(input, frames));
}

TEST(ReplayEyesTest, FailsWhenTheInputCannotBeRead) {
  std::istringstream input(Line(1000) + "\n");
  input.setstate(std::ios::badbit);
  std::vector<EyesFrame> frames;
  EXPECT_FALSE(ReplayEyes(input, frames));
}

}  // namespace
}  // namespace facepalm
