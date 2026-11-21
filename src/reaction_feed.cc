#include "src/reaction_feed.h"

#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/feelings.h"

namespace facepalm {

constexpr int kLowestScore = 1;
constexpr int kHighestScore = 5;

ReactionFeed::ReactionFeed(int capacity, OnRating on_rating, Clock clock)
    : capacity_(capacity),
      on_rating_(std::move(on_rating)),
      clock_(std::move(clock)) {}

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
  Expire(client);
  const auto now = clock_();
  for (const auto& [reading, cue] : timeline_) {
    if (reading.id >= client.cursor) {
      delivery.readings.push_back(reading);
      client.rateable[reading.id] = {
          .shown_at = now,
          .summary = reading.summary,
          .published_at_ms = reading.published_at_ms};
    }
  }
  client.cursor = next_;
  return delivery;
}

// ReactionFeed!Connect.
bool ReactionFeed::Connect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  Client& entry = clients_[client];
  if (entry.connected) {
    return false;
  }
  entry.connected = true;
  entry.cursor = next_;
  return true;
}

// ReactionFeed!Disconnect.
void ReactionFeed::Disconnect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  Client& entry = clients_[client];
  entry.connected = false;
  entry.waiting = false;
  if (entry.wait_canceled) {
    *entry.wait_canceled = true;
  }
  changed_.notify_all();
}

void ReactionFeed::DisconnectAndForget(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  const auto found = clients_.find(client);
  if (found == clients_.end()) {
    return;
  }
  if (found->second.wait_canceled) {
    *found->second.wait_canceled = true;
  }
  clients_.erase(found);
  changed_.notify_all();
}

// ReactionFeed!React, and FeelingBeat!React: the reading keeps the cue its
// detail gives.
std::int64_t ReactionFeed::React(std::string summary, std::string detail,
                                 std::optional<std::int64_t> published_at_ms) {
  const std::scoped_lock lock(mutex_);
  const std::int64_t id = next_++;
  const Cue cue = CueOf(detail);
  timeline_.push_back({.reading = {.id = id,
                                   .summary = std::move(summary),
                                   .detail = std::move(detail),
                                   .published_at_ms = published_at_ms},
                       .cue = cue});
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
  const auto canceled = std::make_shared<bool>(false);
  entry->wait_canceled = canceled;
  changed_.wait_for(lock, timeout,
                    [&] { return *canceled || entry->cursor < next_; });
  if (*canceled) {
    return std::nullopt;
  }
  entry->waiting = false;
  entry->wait_canceled.reset();
  return Tell(*entry);
}

// FeelingBeat!Mark, and FeelingBeat!EndBeat when the window has passed. The
// beat waits out its whole window: a reading wakes it, but does not end it.
// Disconnect and DisconnectAndForget end it unanswered
// (GuardCancelOnLeave).
std::optional<ReactionFeed::Delivery> ReactionFeed::Beat(
    const std::string& client, BeatKind kind,
    std::chrono::milliseconds window) {
  std::unique_lock lock(mutex_);
  Client* entry = Ready(client);
  if (entry == nullptr) {
    return std::nullopt;
  }
  // The window opens at the next reading (GuardWindowStart); the cursor
  // stays, so the reply tells the backlog too (GuardKeepBacklog).
  const std::int64_t start = next_;
  entry->waiting = true;
  const auto canceled = std::make_shared<bool>(false);
  entry->wait_canceled = canceled;
  changed_.wait_for(lock, window, [&] { return *canceled; });
  if (*canceled) {
    return std::nullopt;
  }
  // FeelingBeat!EndBeat: one answer, and the beat closed (GuardCloseBeat).
  entry->waiting = false;
  entry->wait_canceled.reset();
  const Feeling feeling = Answer(start);
  Delivery delivery = Tell(*entry);
  delivery.interpretation = Interpretation{
      .id = next_interpretation_++, .feeling = feeling, .beat = kind};
  return delivery;
}

Feeling ReactionFeed::Answer(std::int64_t start) const {
  const std::int64_t oldest = next_ - std::ssize(timeline_);
  std::vector<Cue> cues;
  for (const auto& [reading, cue] : timeline_) {
    if (reading.id >= start) {
      cues.push_back(cue);
    }
  }
  // A reading of the window dropped before it ended (GuardDroppedUnknown).
  return Interpret(cues, /*complete=*/start >= oldest);
}

void ReactionFeed::Expire(Client& client) {
  const auto now = clock_();
  std::erase_if(client.rateable, [&](const auto& entry) {
    return now - entry.second.shown_at > kRatingWindow;
  });
}

bool ReactionFeed::Waiting(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  const auto found = clients_.find(client);
  return found != clients_.end() && found->second.waiting;
}

// ReactionFeed!Rate.
bool ReactionFeed::Rate(const std::string& client, std::int64_t id, int score) {
  const auto rating = PrepareRating(client, id, score);
  return rating.has_value() && on_rating_(*rating);
}

std::optional<ReactionFeed::Rating> ReactionFeed::PrepareRating(
    const std::string& client, std::int64_t id, int score) {
  const std::scoped_lock lock(mutex_);
  const auto found = clients_.find(client);
  if (found == clients_.end() || !found->second.connected) {
    return std::nullopt;
  }
  Expire(found->second);
  const auto item = found->second.rateable.find(id);
  if (item == found->second.rateable.end() || score < kLowestScore ||
      score > kHighestScore) {
    return std::nullopt;
  }
  return Rating{.client = client,
                .id = id,
                .score = score,
                .summary = item->second.summary,
                .published_at_ms = item->second.published_at_ms};
}

}  // namespace facepalm
