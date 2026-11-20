#ifndef FACEPALM_SRC_PROTOCOL_H_
#define FACEPALM_SRC_PROTOCOL_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "src/feed.h"

// What a client and facepalm's service say to each other (#37): a request
// is one line of text, and each reply is one line of JSON.
//
//   hello <name>          names the client; the first line of a connection
//   read                  the readings since this client last looked
//   wait <milliseconds>   as read, waiting up to 60 seconds for one
//   rate <id> <score>     rates a reading this client was shown, 1 to 5
namespace facepalm::protocol {

struct Hello {
  std::string name;
};
struct ReadRequest {};
struct WaitRequest {
  std::chrono::milliseconds timeout;
};
struct RateRequest {
  std::int64_t id;
  int score;
};

using Request = std::variant<Hello, ReadRequest, WaitRequest, RateRequest>;

// The request on `line`, or nullopt with `error` saying why not.
std::optional<Request> Parse(std::string_view line, std::string& error);

// {"readings":[{"id":..,"summary":"..","detail":{..}}],"missed":n}. A
// reading's detail is JSON facepalm wrote, so it goes in as it is.
// Live readings also carry published_at_ms as Unix epoch milliseconds.
// The reply to a request that worked and has nothing to return.
inline constexpr std::string_view kOkJson = R"({"ok":true})";

// Encodes a JSON string, including quotes and control-character escapes.
std::string Quoted(std::string_view text);

std::string DeliveryJson(const Feed::Delivery& delivery);
std::string ErrorJson(std::string_view message);

}  // namespace facepalm::protocol

#endif  // FACEPALM_SRC_PROTOCOL_H_
