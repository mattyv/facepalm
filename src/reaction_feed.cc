#include "src/reaction_feed.h"

#include <chrono>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace facepalm {

constexpr int kLowestScore = 1;
constexpr int kHighestScore = 5;

ReactionFeed::ReactionFeed(int capacity, OnRating on_rating)
    : capacity_(capacity), on_rating_(std::move(on_rating)) {}

ReactionFeed::Client* ReactionFeed::Ready(const std::string& name) {
  const auto found = clients_.find(name);
  if (found == clients_.end() || !found->second.connected ||
      found->second.waiting) {
    return nullptr;
  }
  return &found->second;
}

ReactionFeed::Delivery ReactionFeed::Tell(Client& client) {
  Delivery delivery;
  const std::int64_t oldest = next_ - std::ssize(timeline_);
  if (client.cursor < oldest) {
    delivery.missed = static_cast<int>(oldest - client.cursor);
  }
  for (const Reading& reading : timeline_) {
    if (reading.id >= client.cursor) {
      delivery.readings.push_back(reading);
      client.shown.insert(reading.id);
    }
  }
  client.cursor = next_;
  return delivery;
}

// ReactionFeed!Connect.
void ReactionFeed::Connect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  Client& entry = clients_[client];
  if (!entry.connected) {
    entry.connected = true;
    entry.cursor = next_;
  }
}

// ReactionFeed!Disconnect.
void ReactionFeed::Disconnect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  Client& entry = clients_[client];
  entry.connected = false;
  entry.waiting = false;
  changed_.notify_all();
}

// ReactionFeed!React.
std::int64_t ReactionFeed::React(std::string summary, std::string detail) {
  const std::scoped_lock lock(mutex_);
  const std::int64_t id = next_++;
  timeline_.push_back(
      {.id = id, .summary = std::move(summary), .detail = std::move(detail)});
  if (std::ssize(timeline_) > capacity_) {
    timeline_.pop_front();
  }
  changed_.notify_all();
  return id;
}

// ReactionFeed!Read.
std::optional<ReactionFeed::Delivery> ReactionFeed::Read(
    const std::string& client) {
  const std::scoped_lock lock(mutex_);
  Client* entry = Ready(client);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return Tell(*entry);
}

// ReactionFeed!Wait, and its end by React, Disconnect or WaitTimesOut.
std::optional<ReactionFeed::Delivery> ReactionFeed::Wait(
    const std::string& client, std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  Client* entry = Ready(client);
  if (entry == nullptr) {
    return std::nullopt;
  }
  entry->waiting = true;
  changed_.wait_for(lock, timeout,
                    [&] { return !entry->waiting || entry->cursor < next_; });
  if (!entry->connected) {
    return std::nullopt;
  }
  entry->waiting = false;
  return Tell(*entry);
}

bool ReactionFeed::Waiting(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  const auto found = clients_.find(client);
  return found != clients_.end() && found->second.waiting;
}

// ReactionFeed!Rate.
bool ReactionFeed::Rate(const std::string& client, std::int64_t id, int score) {
  {
    const std::scoped_lock lock(mutex_);
    const auto found = clients_.find(client);
    if (found == clients_.end() || !found->second.connected ||
        !found->second.shown.contains(id) || score < kLowestScore ||
        score > kHighestScore) {
      return false;
    }
  }
  on_rating_({.client = client, .id = id, .score = score});
  return true;
}

}  // namespace facepalm
