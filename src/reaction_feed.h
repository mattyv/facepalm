#ifndef FACEPALM_SRC_REACTION_FEED_H_
#define FACEPALM_SRC_REACTION_FEED_H_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "src/feed.h"

namespace facepalm {

// How readings reach each client (#36), following specs/ReactionFeed.tla.
// One timeline of readings, each an interpreted reaction with the summary
// line facepalm writes, keeps the last `capacity`. Each client, an agent
// session or AI Pair, reads what came since it last looked, or waits for
// the next reading. The person rates a reading the client was shown, 1-5.
// Thread-safe.
class ReactionFeed : public Feed {
 public:
  struct Rating {
    std::string client;
    std::int64_t id;
    int score;
  };

  using OnRating = std::function<void(const Rating&)>;

  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  ReactionFeed(int capacity, OnRating on_rating,
               Clock clock = std::chrono::steady_clock::now);

  // Connect: a client starts with no backlog. False, changing nothing, when
  // the name is already connected (specs/Service.tla's UniqueNames).
  [[nodiscard]] bool Connect(const std::string& client) override;
  // Disconnect: ends the client's wait.
  void Disconnect(const std::string& client) override;
  // React: adds a reading, drops the oldest past capacity, and answers every
  // waiting client. Returns the reading's id.
  std::int64_t React(std::string summary, std::string detail);
  // Read: what came since the client last looked; nullopt when it is not
  // connected or is waiting.
  std::optional<Delivery> Read(const std::string& client) override;
  // Wait: as Read, but with nothing new, waits up to `timeout` for a
  // reading. nullopt when not connected, already waiting, or disconnected
  // while waiting.
  std::optional<Delivery> Wait(const std::string& client,
                               std::chrono::milliseconds timeout) override;
  [[nodiscard]] bool Waiting(const std::string& client);
  // How long after a reading is shown it can be rated.
  static constexpr std::chrono::minutes kRatingWindow{5};

  // Rate: true, and passed on, only for a reading this client was shown in
  // the last kRatingWindow, and
  // a score from 1 to 5.
  bool Rate(const std::string& client, std::int64_t id, int score) override;

 private:
  struct Client {
    bool connected = false;
    bool waiting = false;
    std::int64_t cursor = 0;  // The first id not yet told about.
    // When each reading was shown, while it can still be rated; kept across
    // reconnects.
    std::map<std::int64_t, std::chrono::steady_clock::time_point> rateable;
  };

  // The client, if it is connected and not waiting.
  Client* Ready(const std::string& name);
  // Tell: the readings since the client's cursor, the dropped ones counted
  // as missed, and the cursor moved past them all.
  Delivery Tell(Client& client);
  // ReactionFeed!Expire: forgets what the client was shown too long ago to
  // rate.
  void Expire(Client& client);

  const int capacity_;
  const OnRating on_rating_;
  const Clock clock_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<Reading> timeline_;  // Oldest first.
  std::int64_t next_ = 1;
  std::map<std::string, Client> clients_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_REACTION_FEED_H_
