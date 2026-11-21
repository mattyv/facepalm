#include "src/approved_feed.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "src/feelings.h"

namespace facepalm {

ApprovedFeed::ApprovedFeed(Feed& feed, std::string token,
                           std::function<void(bool)> changed,
                           std::function<void(const std::string&)> retire,
                           bool ratings_enabled)
    : feed_(feed),
      token_(std::move(token)),
      changed_(std::move(changed)),
      retire_(std::move(retire)),
      ratings_enabled_(ratings_enabled) {}

bool ApprovedFeed::Connect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  if (client != token_ || !session_.empty()) {
    return false;
  }
  const std::string name = "approved-" + std::to_string(++next_session_);
  if (!feed_.Connect(name)) {
    return false;
  }
  session_ = name;
  changed_(true);
  return true;
}

void ApprovedFeed::Disconnect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  if (client != token_ || session_.empty()) {
    return;
  }
  retire_(session_);
  session_.clear();
  changed_(false);
}

std::string ApprovedFeed::Session(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  return client == token_ ? session_ : std::string{};
}

std::optional<Feed::Delivery> ApprovedFeed::Read(const std::string& client) {
  const std::string session = Session(client);
  return session.empty() ? std::nullopt : feed_.Read(session);
}

std::optional<Feed::Delivery> ApprovedFeed::Wait(
    const std::string& client, std::chrono::milliseconds timeout) {
  const std::string session = Session(client);
  return session.empty() ? std::nullopt : feed_.Wait(session, timeout);
}

std::optional<Feed::Delivery> ApprovedFeed::Beat(
    const std::string& client, BeatKind kind,
    std::chrono::milliseconds window) {
  const std::string session = Session(client);
  return session.empty() ? std::nullopt : feed_.Beat(session, kind, window);
}

bool ApprovedFeed::Rate(const std::string& client, std::int64_t id, int score) {
  const std::string session = Session(client);
  return ratings_enabled_ && !session.empty() && feed_.Rate(session, id, score);
}

}  // namespace facepalm
