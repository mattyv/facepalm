#ifndef FACEPALM_SRC_REACTION_FEED_H_
#define FACEPALM_SRC_REACTION_FEED_H_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "src/feed.h"
#include "src/feed_clients.h"
#include "src/feelings.h"
#include "src/screen_context.h"
#include "src/tuner.h"

namespace facepalm {

// How readings reach each client (#36), following specs/ReactionFeed.tla.
// One timeline of readings, each an interpreted reaction with the summary
// line facepalm writes, keeps the last `capacity`. Each client, an agent
// session or AI Pair, reads what came since it last looked, or waits for
// the next reading, or marks a beat and is told how its reaction was read,
// following specs/FeelingBeat.tla, read as the person's own picks say
// (`tuner`). The person then says how they really felt, which goes back to
// `tuner`, following specs/FeelingFeedback.tla. Thread-safe.
class ReactionFeed : public Feed {
 public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;
  // Milliseconds since the Unix epoch (src/feed_clients.h's own
  // UnixMilliseconds, the default): the clock a beat's own start and end
  // are marked on for GazeWindowFn below (#142), the same basis a
  // reading's published_at_ms already uses, kept apart from `clock_`
  // above since that one never needs to agree with a camera frame's own
  // sample time, only with itself across a feeling's five-minute window.
  using NowMs = std::function<std::int64_t()>;
  // Reads the majority gaze, and, given a content rectangle, whether it
  // was mostly inside it, over [start_ms, end_ms) (#142, #144):
  // src/screen_context_reporter.h's Between in the daemon, mocked in
  // tests. nullptr (the default) answers every beat Gaze::kUnknown with
  // no content, for a daemon with screen context disabled, or a test
  // with no gaze source to give.
  using GazeWindowFn =
      std::function<GazeWindow(std::int64_t start_ms, std::int64_t end_ms,
                               const std::optional<ContentRect>& content)>;

  ReactionFeed(int capacity, Tuner& tuner,
               Clock clock = std::chrono::steady_clock::now,
               NowMs now_ms = UnixMilliseconds,
               GazeWindowFn gaze_window = nullptr);

  // Connect: a client starts with no backlog. False, changing nothing, when
  // the name is already connected (specs/Service.tla's UniqueNames).
  [[nodiscard]] bool Connect(const std::string& client) override;
  // Disconnect: ends the client's wait, and forgets the interpretations it
  // was given.
  void Disconnect(const std::string& client) override;
  // React: adds a reading, drops the oldest past capacity, and answers every
  // waiting client. Returns the reading's id. `context` is Feed::Reading's
  // screen context (#61 item 11), carried through as given; it plays no
  // part in the reading's cue, which CueOf still reads from `detail`
  // alone.
  std::int64_t React(std::string summary, std::string detail,
                     std::optional<std::int64_t> published_at_ms = std::nullopt,
                     std::optional<std::string> context = std::nullopt);
  // Read: what came since the client last looked; nullopt when it is not
  // connected or is waiting.
  std::optional<Delivery> Read(const std::string& client) override;
  // Wait: as Read, but with nothing new, waits up to `timeout` for a
  // reading. nullopt when not connected, already waiting, or disconnected
  // while waiting.
  std::optional<Delivery> Wait(const std::string& client,
                               std::chrono::milliseconds timeout) override;
  // Beat: FeelingBeat!Mark, then FeelingBeat!EndBeat once `window` has
  // passed. nullopt when not connected, already waiting, or disconnected
  // during the beat (FeelingBeat!Disconnect). The interpretation's own
  // gaze and content (#142, #144) come from gaze_window_, asked for
  // [start, end) on now_ms_'s own clock; a question beat gaze_window_
  // reads as averted at the end reads unknown however the cues went
  // (FeelingBeat.tla's GuardAvertedUnknown, the thinking guard), never
  // any other feeling gaze would not otherwise have given. The same gaze
  // is folded into the evidence given to tuner_.Read and later Feel
  // (#143), so the personal read tunes on it too.
  std::optional<Delivery> Beat(const std::string& client, BeatKind kind,
                               std::chrono::milliseconds window,
                               std::optional<ContentRect> content) override;
  // Whether the client is waiting or in a beat.
  [[nodiscard]] bool Waiting(const std::string& client);
  // Disconnects an ephemeral identity and removes its history. An older
  // wait holds its own cancellation token and returns no delivery.
  void DisconnectAndForget(const std::string& client);
  // How long after a beat ends the person can say how they felt.
  static constexpr std::chrono::minutes kFeelingWindow{5};

  // FeelingFeedback!Feel: true, and learned, only for an interpretation
  // this client was given in the last kFeelingWindow, the first time, and
  // once the tuner has saved it. A beat whose evidence could not say which
  // reaction it was about takes the feeling, but teaches nothing.
  bool Feel(const std::string& client, std::int64_t interpretation,
            Feeling feeling) override;
  // FeelingFeedback!Reset, for a connected client.
  bool Reset(const std::string& client) override;

 private:
  struct Client {
    bool connected = false;
    std::shared_ptr<bool> wait_canceled;
    bool waiting = false;
    std::int64_t cursor = 0;  // The first id not yet told about.
    // The interpretations it was given that can still take a feeling: when
    // each was given, and its evidence.
    struct Given {
      std::chrono::steady_clock::time_point given_at;
      std::optional<Evidence> evidence;
    };
    std::map<std::int64_t, Given> given;
  };

  // What a beat's window showed.
  struct Window {
    Feeling generic;
    std::optional<Evidence> evidence;
  };

  // The client, if it is connected and not waiting.
  Client* Ready(const std::string& name);
  // Tell: the readings since the client's cursor, the dropped ones counted
  // as missed, and the cursor moved past them all.
  Delivery Tell(Client& client);
  // FeelingFeedback!Expire: forgets the interpretations the client was
  // given too long ago to take a feeling.
  void Expire(Client& client);

  // FeelingBeat!Answer: the generic feeling of the readings from id `start`
  // on, and their evidence.
  [[nodiscard]] Window Answer(std::int64_t start, BeatKind kind) const;

  struct Entry {
    Reading reading;
    Cue cue;
    std::string sign;  // SignOf the reading's detail.
  };

  const int capacity_;
  Tuner& tuner_;
  const Clock clock_;
  const NowMs now_ms_;
  const GazeWindowFn gaze_window_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<Entry> timeline_;  // Oldest first.
  std::int64_t next_ = 1;
  std::int64_t next_interpretation_ = 1;
  std::map<std::string, Client> clients_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_REACTION_FEED_H_
