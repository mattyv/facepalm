#ifndef FACEPALM_SRC_FEED_H_
#define FACEPALM_SRC_FEED_H_

#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "src/feelings.h"
#include "src/screen_context.h"

namespace facepalm {

// What the service asks of the reaction feed for its clients (#37);
// ReactionFeed implements it, and specs/ReactionFeed.tla describes it.
class Feed {
 public:
  struct Reading {
    std::int64_t id;
    std::string summary;  // The one line the person sees.
    std::string detail;   // The full interpreted context, as JSON.
    std::optional<std::int64_t> published_at_ms = std::nullopt;
    // What the screen showed at the moment of the reading (#61 item 11):
    // active app, mouse and coarse gaze, as JSON text, when
    // Config::screen_context_enabled; nullopt otherwise.
    std::optional<std::string> context = std::nullopt;
  };

  // How a beat's reaction was read (specs/FeelingBeat.tla). `gaze` is the
  // majority coarse gaze over the beat's own window (#142), context only:
  // it never changes `feeling` by rule, the one exception being the
  // thinking guard a question beat's own averted gaze applies (#142's
  // own GuardAvertedUnknown, src/reaction_feed.cc's Beat). `content` is
  // only given when the client's beat carried a content rectangle (#144).
  struct Interpretation {
    std::int64_t id;  // Unique among this run's interpretations.
    Feeling feeling;
    BeatKind beat;  // What the client marked the beat after.
    Gaze gaze = Gaze::kUnknown;
    std::optional<ContentHit> content = std::nullopt;
    friend auto operator<=>(const Interpretation&,
                            const Interpretation&) = default;
  };

  struct Delivery {
    std::vector<Reading> readings;
    int missed = 0;  // Readings dropped before this client saw them.
    // Only in the reply that ends a beat.
    std::optional<Interpretation> interpretation = std::nullopt;
  };

  virtual ~Feed() = default;

  // False when the name is already connected.
  [[nodiscard]] virtual bool Connect(const std::string& client) = 0;
  // Ends the client's wait.
  virtual void Disconnect(const std::string& client) = 0;
  // nullopt when the client is not connected or is waiting.
  virtual std::optional<Delivery> Read(const std::string& client) = 0;
  // As Read, waiting up to `timeout` for a reading; nullopt also when the
  // client is disconnected while waiting.
  virtual std::optional<Delivery> Wait(const std::string& client,
                                       std::chrono::milliseconds timeout) = 0;
  // Marks a beat after a question or a proposal, and waits out its
  // `window`: as Read, with the interpretation of the readings made in the
  // window. nullopt when the client is not connected, is waiting, or is
  // disconnected during the beat. `content` is a content rectangle
  // (#144) the client's question or proposal occupies on screen, for
  // the interpretation's own `content` field; std::nullopt for none
  // (no default: Google's style forbids one on a virtual, so every
  // caller gives it explicitly).
  virtual std::optional<Delivery> Beat(const std::string& client, BeatKind kind,
                                       std::chrono::milliseconds window,
                                       std::optional<ContentRect> content) = 0;
  // The person's feeling about an interpretation this client was given
  // (specs/FeelingFeedback.tla): true only for one given in the last five
  // minutes, the first time, and once the feeling is saved.
  virtual bool Feel(const std::string& client, std::int64_t interpretation,
                    Feeling feeling) = 0;
  // Forgets every feeling the person gave. True once that is saved.
  virtual bool Reset(const std::string& client) = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FEED_H_
