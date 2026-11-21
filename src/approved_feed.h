#ifndef FACEPALM_SRC_APPROVED_FEED_H_
#define FACEPALM_SRC_APPROVED_FEED_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "src/feed.h"
#include "src/feelings.h"

namespace facepalm {

// One approved secret admits one client; the inner feed never sees the secret.
class ApprovedFeed : public Feed {
 public:
  ApprovedFeed(Feed& feed, std::string token, std::function<void(bool)> changed,
               std::function<void(const std::string&)> retire,
               bool ratings_enabled = true);
  bool Connect(const std::string& client) override;
  void Disconnect(const std::string& client) override;
  std::optional<Delivery> Read(const std::string& client) override;
  std::optional<Delivery> Wait(const std::string& client,
                               std::chrono::milliseconds timeout) override;
  std::optional<Delivery> Beat(const std::string& client, BeatKind kind,
                               std::chrono::milliseconds window) override;
  bool Rate(const std::string& client, std::int64_t id, int score) override;

 private:
  Feed& feed_;
  const std::string token_;
  std::function<void(bool)> changed_;
  std::function<void(const std::string&)> retire_;
  bool ratings_enabled_;
  std::mutex mutex_;
  std::string session_;             // Under mutex_; empty when disconnected.
  std::uint64_t next_session_ = 0;  // Under mutex_.
  std::string Session(const std::string& client);
};

}  // namespace facepalm
#endif  // FACEPALM_SRC_APPROVED_FEED_H_
