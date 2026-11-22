#ifndef FACEPALM_SRC_PREVIEW_READINGS_H_
#define FACEPALM_SRC_PREVIEW_READINGS_H_

#include <cstdint>
#include <mutex>
#include <string>

#include "src/feelings.h"

namespace facepalm {

// The camera preview's own text (#134), beside the frame: the readings as
// they happen, each held briefly so it can be read, and the latest beat's
// feeling. Holds only this text; a caller publishes it through
// PreviewLease::Publish the same way src/preview_publisher.cc already
// does for the frame, so this never decides on its own whether the
// preview is streaming. Thread-safe: OnReading reaches this from the
// control queue (the same call that builds FeedClients::Deliver's own
// summary), OnBeat from whichever queue answers a client's beat, which is
// not necessarily the same one, and Text from the camera's capture
// queue, once a frame, alongside the frame's own publish.
class PreviewReadings {
 public:
  // How long a reading stays on display once nothing newer replaces it
  // (the issue's "each held briefly so it can be read").
  static constexpr std::int64_t kHoldMs = 2000;

  // A recognized reaction's summary, such as "Gesture: nod", at the wall
  // clock time it was delivered (FeedClients::Deliver's own `now_()`,
  // UnixMilliseconds). An empty summary -- Deliver's own "nothing
  // recognized" case -- changes nothing: whatever is already showing,
  // if anything, keeps its hold.
  void OnReading(std::string summary, std::int64_t at_ms);

  // The latest beat's feeling, such as "Last read: confused (question)".
  // Never expires on its own: it is the most recent beat for as long as
  // the preview keeps streaming, not a momentary reading.
  void OnBeat(Feeling feeling, BeatKind kind);

  // Both files share this lifecycle (#134): cleared whenever
  // PreviewLease's own streaming_changed hook clears the frame, so a
  // stale reading or feeling from an earlier stream never reappears the
  // moment the next one starts.
  void Clear();

  // The text to publish at wall-clock `now_ms`: the current reading, if
  // it is still within kHoldMs of when it was delivered, then the latest
  // beat's feeling, each on its own line, omitted while it has nothing
  // to show. Empty when neither has happened yet (or both were cleared).
  [[nodiscard]] std::string Text(std::int64_t now_ms) const;

 private:
  mutable std::mutex mutex_;
  std::string reading_;             // Under mutex_.
  std::int64_t reading_at_ms_ = 0;  // Under mutex_.
  std::string feeling_line_;        // Under mutex_.
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_PREVIEW_READINGS_H_
