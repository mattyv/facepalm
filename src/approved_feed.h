#ifndef FACEPALM_SRC_APPROVED_FEED_H_
#define FACEPALM_SRC_APPROVED_FEED_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "src/feed.h"
#include "src/feelings.h"

namespace facepalm {

// The feed for the clients Service let in with an approved token (#37),
// following specs/DaemonAdmission.tla. Each connection is a session of its
// own in the inner feed, which forgets it once it leaves, and every session
// reads the same readings. The camera has one subscription for them all:
// `changed` is told true when the first client joins and false when the
// last one leaves (GuardFirstLast).
class ApprovedFeed : public Feed {
 public:
  // `on_beat`, when given, hears every interpretation a Beat resolves
  // with (#134): the camera preview's own "latest beat's feeling" is the
  // one caller today (src/preview_readings.h's OnBeat), so this stays
  // optional rather than every other use of ApprovedFeed having to pass
  // one that does nothing.
  ApprovedFeed(
      Feed& feed, std::function<void(bool)> changed,
      std::function<void(const std::string&)> retire,
      std::function<void(const Feed::Interpretation&)> on_beat = nullptr);
  // DaemonAdmission!Join.
  bool Connect(const std::string& client) override;
  // DaemonAdmission!Disconnect.
  void Disconnect(const std::string& client) override;
  std::optional<Delivery> Read(const std::string& client) override;
  std::optional<Delivery> Wait(const std::string& client,
                               std::chrono::milliseconds timeout) override;
  std::optional<Delivery> Beat(const std::string& client, BeatKind kind,
                               std::chrono::milliseconds window) override;
  bool Feel(const std::string& client, std::int64_t interpretation,
            Feeling feeling) override;
  bool Reset(const std::string& client) override;

 private:
  // The client's session, or "" when it is not connected.
  std::string Session(const std::string& client);

  Feed& feed_;
  std::function<void(bool)> changed_;
  std::function<void(const std::string&)> retire_;
  std::function<void(const Feed::Interpretation&)> on_beat_;
  std::mutex mutex_;
  std::map<std::string, std::string> sessions_;  // Under mutex_.
  std::uint64_t next_session_ = 0;               // Under mutex_.
};

}  // namespace facepalm
#endif  // FACEPALM_SRC_APPROVED_FEED_H_
