#include "src/reaction_feed.h"

#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/eye_signs.h"
#include "src/feelings.h"
#include "src/tuner.h"

namespace facepalm {

ReactionFeed::ReactionFeed(int capacity, Tuner& tuner, Clock clock,
                           NowMs now_ms, GazeWindowFn gaze_window,
                           EyeSignsWindowFn eye_signs_window)
    : capacity_(capacity),
      tuner_(tuner),
      clock_(std::move(clock)),
      now_ms_(std::move(now_ms)),
      gaze_window_(std::move(gaze_window)),
      eye_signs_window_(std::move(eye_signs_window)) {}

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
  for (const Entry& entry : timeline_) {
    if (entry.reading.id >= client.cursor) {
      delivery.readings.push_back(entry.reading);
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

// ReactionFeed!Disconnect. The interpretations go too: a feeling is taken
// only for one given to this connection (FeelingFeedback's GuardGivenOnly).
void ReactionFeed::Disconnect(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  Client& entry = clients_[client];
  entry.connected = false;
  entry.waiting = false;
  entry.given.clear();
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
                                 std::optional<std::int64_t> published_at_ms,
                                 std::optional<std::string> context) {
  const std::scoped_lock lock(mutex_);
  const std::int64_t id = next_++;
  const Cue cue = CueOf(detail);
  const std::string sign(SignOf(detail));
  timeline_.push_back({.reading = {.id = id,
                                   .summary = std::move(summary),
                                   .detail = std::move(detail),
                                   .published_at_ms = published_at_ms,
                                   .context = std::move(context)},
                       .cue = cue,
                       .sign = sign});
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
// (GuardCancelOnLeave). The answer is given to the client
// (FeelingFeedback!Give), and read as the person's picks say, outside the
// feed's lock, since the tuner may be saving.
std::optional<ReactionFeed::Delivery> ReactionFeed::Beat(
    const std::string& client, BeatKind kind, std::chrono::milliseconds window,
    std::optional<ContentRect> content) {
  std::unique_lock lock(mutex_);
  Client* entry = Ready(client);
  if (entry == nullptr) {
    return std::nullopt;
  }
  // The window opens at the next reading (GuardWindowStart); the cursor
  // stays, so the reply tells the backlog too (GuardKeepBacklog).
  const std::int64_t start = next_;
  const std::int64_t started_ms = now_ms_();
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
  const std::int64_t ended_ms = now_ms_();
  const Window shown = Answer(start, kind);
  Delivery delivery = Tell(*entry);
  const std::int64_t id = next_interpretation_++;
  Expire(*entry);
  // The beat's own gaze (#142, #144), folded into the evidence (#143)
  // before it is given or read, so the personal read and the later Feel
  // (FeelingFeedback!Feel) both tune on the same gaze the reply reports.
  const GazeWindow gaze_window =
      gaze_window_ ? gaze_window_(started_ms, ended_ms, content) : GazeWindow{};
  // The same window's blink rate, long blink and widened eyes (#145),
  // folded in the same way (#165): a feature the personal read alone
  // weighs, never Rule's own input (gaze_window.gaze's own thinking
  // guard below is unaffected).
  const EyeSigns eye_signs =
      eye_signs_window_ ? eye_signs_window_(started_ms, ended_ms) : EyeSigns{};
  std::optional<Evidence> evidence = shown.evidence;
  if (evidence) {
    evidence->gaze = gaze_window.gaze;
    evidence->blink_rate = eye_signs.blink_rate;
    evidence->long_blink = eye_signs.long_blink;
    evidence->widened_eyes = eye_signs.widened_eyes;
  }
  entry->given[id] = {.given_at = clock_(), .evidence = evidence};
  lock.unlock();
  const Feeling generic =
      evidence ? tuner_.Read(*evidence, shown.generic) : shown.generic;
  // FeelingBeat.tla's GuardAvertedUnknown, the thinking guard (#142): a
  // question's own gaze averted at the end is read unknown however the
  // cues went -- "eyes never decide a feeling by rule" otherwise, which
  // is why gaze plays no other part in `feeling` here.
  const bool thinking_guard =
      kind == BeatKind::kQuestion && gaze_window.gaze == Gaze::kOtherScreen;
  delivery.interpretation =
      Interpretation{.id = id,
                     .feeling = thinking_guard ? Feeling::kUnknown : generic,
                     .beat = kind,
                     .gaze = gaze_window.gaze,
                     .content = gaze_window.content};
  return delivery;
}

ReactionFeed::Window ReactionFeed::Answer(std::int64_t start,
                                          BeatKind kind) const {
  const std::int64_t oldest = next_ - std::ssize(timeline_);
  std::vector<Cue> cues;
  std::vector<std::string_view> signs;
  for (const auto& entry : timeline_) {
    if (entry.reading.id >= start) {
      cues.push_back(entry.cue);
      signs.emplace_back(entry.sign);
    }
  }
  // A reading of the window dropped before it ended (GuardDroppedUnknown).
  const bool complete = start >= oldest;
  return {.generic = Interpret(cues, complete),
          .evidence = EvidenceOf(signs, kind, complete)};
}

void ReactionFeed::Expire(Client& client) {
  const auto now = clock_();
  std::erase_if(client.given, [&](const auto& entry) {
    return now - entry.second.given_at > kFeelingWindow;
  });
}

bool ReactionFeed::Waiting(const std::string& client) {
  const std::scoped_lock lock(mutex_);
  const auto found = clients_.find(client);
  return found != clients_.end() && found->second.waiting;
}

// FeelingFeedback!Feel: only an interpretation given to this client
// (GuardGivenOnly), not yet expired (GuardRecent), and taken once
// (GuardOnce); then FeelingFeedback!Begin, in the tuner, outside the feed's
// lock.
bool ReactionFeed::Feel(const std::string& client, std::int64_t interpretation,
                        Feeling feeling) {
  std::optional<Evidence> evidence;
  {
    const std::scoped_lock lock(mutex_);
    const auto found = clients_.find(client);
    if (found == clients_.end() || !found->second.connected) {
      return false;
    }
    Expire(found->second);
    const auto given = found->second.given.find(interpretation);
    if (given == found->second.given.end()) {
      return false;
    }
    evidence = std::move(given->second.evidence);
    found->second.given.erase(given);
  }
  return !evidence || tuner_.Learn(*evidence, feeling);
}

// FeelingFeedback!Reset.
bool ReactionFeed::Reset(const std::string& client) {
  {
    const std::scoped_lock lock(mutex_);
    const auto found = clients_.find(client);
    if (found == clients_.end() || !found->second.connected) {
      return false;
    }
  }
  return tuner_.Forget();
}

}  // namespace facepalm
