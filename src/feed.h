#ifndef FACEPALM_SRC_FEED_H_
#define FACEPALM_SRC_FEED_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace facepalm {

// What the service asks of the reaction feed for its clients (#37);
// ReactionFeed implements it, and specs/ReactionFeed.tla describes it.
class Feed {
 public:
  struct Reading {
    std::int64_t id;
    std::string summary;  // The one line the person sees and rates.
    std::string detail;   // The full context the agent gets, as JSON.
  };

  struct Delivery {
    std::vector<Reading> readings;
    int missed = 0;  // Readings dropped before this client saw them.
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
  // True only for a reading this client was shown and a score from 1 to 5.
  virtual bool Rate(const std::string& client, std::int64_t id, int score) = 0;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_FEED_H_
