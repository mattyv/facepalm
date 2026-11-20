#include "src/gesture_replay.h"

#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

#include "src/config.h"
#include "src/gesture_reader.h"
#include "src/head_gesture_model.h"

namespace facepalm {

// One pass over the log.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool ReplayGestures(std::istream& input, std::ostream& output) {
  head_gesture::Window window;
  GestureReportGate gate;
  std::int64_t last = -1;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    std::string kind;
    row >> kind;
    if (kind == "gesture_no_face") {
      window.Reset();
      gate.Reset();
    } else if (kind == "gesture_raw") {
      std::int64_t timestamp = 0;
      head_gesture::RawFrame frame{};
      if (!(row >> timestamp) || timestamp <= last) {
        return false;
      }
      for (float& value : frame) {
        if (!(row >> value)) {
          return false;
        }
      }
      std::string extra;
      if (row >> extra) {
        return false;
      }
      last = timestamp;
      const auto frames = window.Add(frame, timestamp);
      if (!frames) {
        continue;
      }
      const auto prediction = head_gesture::Predict(*frames);
      for (const float probability : prediction.probabilities) {
        if (!std::isfinite(probability)) {
          return false;
        }
      }
      const auto classified = head_gesture::Classify(
          prediction, Config::kDefaultGestureConfidenceThreshold);
      const auto reported = gate.Update(
          classified, timestamp,
          {.yaw = frame.at(15), .window_start_yaw = frames->front().at(15)});
      output << "gesture_probs " << timestamp;
      for (const float probability : prediction.probabilities) {
        output << ' ' << probability;
      }
      output << " class " << static_cast<int>(classified) << " report "
             << static_cast<int>(reported) << '\n';
    }
  }
  return !input.bad() && output.good();
}

}  // namespace facepalm
