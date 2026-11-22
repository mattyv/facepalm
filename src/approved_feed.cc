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

ApprovedFeed::ApprovedFeed(
    Feed& feed, std::function<void(bool)> changed,
    std::function<void(const std::string&)> retire,
    std::function<void(const Feed::Interpretation&)> on_beat)
    : feed_(feed),
      changed_(std::move(changed)),
      retire_(std::move(retire)),
      on_beat_(std::move(on_beat)) {}

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

std::optional<Feed::Delivery> ApprovedFeed::Beat(
    const std::string& client, BeatKind kind, std::chrono::milliseconds window,
    std::optional<ContentRect> content) {
  const std::string session = Session(client);
  if (session.empty()) {
    return std::nullopt;
  }
  const std::optional<Delivery> delivery =
      feed_.Beat(session, kind, window, content);
  if (delivery && delivery->interpretation && on_beat_) {
    on_beat_(*delivery->interpretation);
  }
  return delivery;
}

bool ApprovedFeed::Feel(const std::string& client, std::int64_t interpretation,
                        Feeling feeling) {
  const std::string session = Session(client);
  return !session.empty() && feed_.Feel(session, interpretation, feeling);
}

bool ApprovedFeed::Reset(const std::string& client) {
  const std::string session = Session(client);
  return !session.empty() && feed_.Reset(session);
}

}  // namespace facepalm
