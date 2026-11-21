#ifndef FACEPALM_SRC_FEED_CLIENTS_H_
#define FACEPALM_SRC_FEED_CLIENTS_H_

#include <cstdint>
#include <functional>
#include <string>

#include "src/camera_controller.h"

namespace facepalm {

// Where the camera's reactions go in the daemon (#37): every approved client
// shares one camera subscription, and each reaction it delivers becomes one
// reading in ReactionFeed, which every client reads. Only interpreted
// gestures, such as {"gesture":"nod"}, and expressions, such as
// {"expression":"brows_down"}, are readings; head angles and anything else
// are not.
class FeedClients : public Clients {
 public:
  // ReactionFeed::React: the reading's summary, its detail, and when it was
  // published, in milliseconds since the Unix epoch.
  using React = std::function<void(std::string summary, std::string detail,
                                   std::int64_t published_ms)>;
  // Milliseconds since the Unix epoch.
  using Clock = std::function<std::int64_t()>;

  FeedClients(React react, Clock now);
  void Deliver(const ClientId& client, const Reaction& reaction) override;

 private:
  React react_;
  Clock now_;
};

// The system clock, in milliseconds since the Unix epoch.
std::int64_t UnixMilliseconds();

}  // namespace facepalm

#endif  // FACEPALM_SRC_FEED_CLIENTS_H_
