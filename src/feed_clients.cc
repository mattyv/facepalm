#include "src/feed_clients.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace facepalm {

FeedClients::FeedClients(React react, Clock now, ContextProvider context)
    : react_(std::move(react)),
      now_(std::move(now)),
      context_(std::move(context)) {}

// specs/CameraLifecycle.tla's Emit reaches the one subscription the daemon
// holds for every client, so each reaction is one reading.
void FeedClients::Deliver(const ClientId& /*client*/,
                          const Reaction& reaction) {
  constexpr std::string_view kGesture = R"({"gesture":")";
  constexpr std::string_view kExpression = R"({"expression":")";
  constexpr std::string_view kEnd = "\"}";
  const std::string_view event(reaction);
  const bool gesture = event.starts_with(kGesture);
  const std::string_view prefix = gesture ? kGesture : kExpression;
  if (!event.starts_with(prefix) ||
      event.size() < prefix.size() + kEnd.size() || !event.ends_with(kEnd)) {
    return;
  }
  const std::string_view name =
      event.substr(prefix.size(), event.size() - prefix.size() - kEnd.size());
  if (name.empty() || name.find('"') != std::string_view::npos) {
    return;
  }
  react_(
      std::string(gesture ? "Gesture: " : "Expression: ") + std::string(name),
      reaction, now_(), context_ ? context_() : std::nullopt);
}

std::int64_t UnixMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace facepalm
