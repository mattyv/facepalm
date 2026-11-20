#include "src/gesture_replay.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <sstream>
#include <string>

namespace {

void Frames(std::ostream& out, int first, int count) {
  for (int i = first; i < first + count; ++i) {
    out << "gesture_raw " << i * 66;
    out << " 92 102 40 132 102 40 112 117 50"
           " 72 112 25 152 112 25 0 0 0";
    out << '\n';
  }
}

TEST(GestureReplayTest, ExplicitNoFaceResetsWindowButTimeGapDoesNot) {
  std::ostringstream log;
  Frames(log, 0, 49);
  log << "unrelated log line\n";
  Frames(log, 100, 1);  // A long gap alone does not reset the live window.
  log << "gesture_no_face\n";
  Frames(log, 101, 49);
  std::istringstream input(log.str());
  std::ostringstream output;
  EXPECT_TRUE(facepalm::ReplayGestures(input, output));
  EXPECT_NE(output.str().find("gesture_probs 6600 "), std::string::npos);
  EXPECT_EQ(output.str().find("gesture_probs 9834 "), std::string::npos);
}

TEST(GestureReplayTest, NoFaceResetsTheReportGate) {
  std::ostringstream log;
  for (int i = 0; i < 120; ++i) {
    if (i == 60) {
      log << "gesture_no_face\n";
    }
    const int step = i % 60;
    const double angle = 5 * std::numbers::pi / 180 *
                         std::sin(2 * std::numbers::pi * 1.5 * step / 15.0);
    log << "gesture_raw " << i * 66;
    for (const auto& point :
         {std::array<double, 3>{-20, -10, 0}, std::array<double, 3>{20, -10, 0},
          std::array<double, 3>{0, 5, 10}, std::array<double, 3>{-40, 0, -15},
          std::array<double, 3>{40, 0, -15}}) {
      log << ' '
          << 112 + (point[0] * std::cos(angle)) - (point[1] * std::sin(angle))
          << ' '
          << 112 + (point[0] * std::sin(angle)) + (point[1] * std::cos(angle))
          << ' ' << 40 + point[2];
    }
    log << " 0 " << angle << " 0\n";
  }
  std::istringstream input(log.str());
  std::ostringstream output;
  ASSERT_TRUE(facepalm::ReplayGestures(input, output));
  const std::string result = output.str();
  const auto first = result.find("report 3\n");
  ASSERT_NE(first, std::string::npos);
  EXPECT_NE(result.find("report 3\n", first + 1), std::string::npos);
  EXPECT_EQ(result.find("report 3\n", result.find("report 3\n", first + 1) + 1),
            std::string::npos);
}

TEST(GestureReplayTest, RejectsDegenerateModelInput) {
  std::ostringstream log;
  for (int i = 0; i < 50; ++i) {
    log << "gesture_raw " << i * 66;
    for (int channel = 0; channel < 18; ++channel) {
      log << " 1";
    }
    log << '\n';
  }
  std::istringstream input(log.str());
  std::ostringstream output;
  EXPECT_FALSE(facepalm::ReplayGestures(input, output));
}

TEST(GestureReplayTest, RejectsMalformedAndNonIncreasingRawFrames) {
  std::istringstream bad("gesture_raw 1 0\n");
  std::ostringstream out;
  EXPECT_FALSE(facepalm::ReplayGestures(bad, out));
  std::ostringstream log;
  Frames(log, 1, 1);
  Frames(log, 1, 1);
  std::istringstream repeated(log.str());
  EXPECT_FALSE(facepalm::ReplayGestures(repeated, out));
  std::istringstream invalid_time("gesture_raw nope\n");
  EXPECT_FALSE(facepalm::ReplayGestures(invalid_time, out));
  std::ostringstream extra_log;
  Frames(extra_log, 1, 1);
  std::string extra = extra_log.str();
  extra.insert(extra.size() - 1, " extra");
  std::istringstream extra_input(extra);
  EXPECT_FALSE(facepalm::ReplayGestures(extra_input, out));
  std::istringstream bad_value("gesture_raw 1 nan\n");
  EXPECT_FALSE(facepalm::ReplayGestures(bad_value, out));
  std::istringstream failed_input;
  failed_input.setstate(std::ios::badbit);
  EXPECT_FALSE(facepalm::ReplayGestures(failed_input, out));
  std::istringstream empty;
  std::ostringstream failed_output;
  failed_output.setstate(std::ios::badbit);
  EXPECT_FALSE(facepalm::ReplayGestures(empty, failed_output));
}

}  // namespace
