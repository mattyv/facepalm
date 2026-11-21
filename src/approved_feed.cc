#include "src/approved_feed.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace facepalm {

ApprovedFeed::ApprovedFeed(Feed& feed, std::function<void(bool)> changed,
                           std::function<void(const std::string&)> retire,
                           bool ratings_enabled)
    : feed_(feed),
      changed_(std::move(changed)),
      retire_(std::move(retire)),
      ratings_enabled_(ratings_enabled) {}

// DaemonAdmission!Join: only the first client changes the subscription.
// Changes are told under the lock, so they reach the control queue in order.
bool ApprovedFeed::Connect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  if (sessions_.contains(client)) {
    return false;
  }
  std::string session = "approved-" + std::to_string(++next_session_);
  if (!feed_.Connect(session)) {
    return false;
  }
  sessions_.emplace(client, std::move(session));
  if (sessions_.size() == 1) {
    changed_(true);
  }
  return true;
}

// DaemonAdmission!Disconnect: only the last client changes the subscription.
void ApprovedFeed::Disconnect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  const auto found = sessions_.find(client);
  if (found == sessions_.end()) {
    return;
  }
  retire_(found->second);
  sessions_.erase(found);
  if (sessions_.empty()) {
    changed_(false);
  }
}

std::string ApprovedFeed::Session(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  const auto found = sessions_.find(client);
  return found == sessions_.end() ? std::string{} : found->second;
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

bool ApprovedFeed::Rate(const std::string& client, std::int64_t id, int score) {
  const std::string session = Session(client);
  return ratings_enabled_ && !session.empty() && feed_.Rate(session, id, score);
}

}  // namespace facepalm
