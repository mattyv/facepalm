#ifndef FACEPALM_SRC_PROTOCOL_H_
#define FACEPALM_SRC_PROTOCOL_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "src/feed.h"
#include "src/feelings.h"

// What a client and facepalm's service say to each other (#37): a request
// is one line of text, and each reply is one line of JSON.
//
//   hello <name> [<token>]  names the client and shows its approval token;
//                           the first line of a connection
//   read                    the readings since this client last looked
//   wait <milliseconds>     as read, waiting up to 60 seconds for one
//   beat <kind> <milliseconds>
//                           marks a beat after a question or a proposal, and
//                           waits out its 1 to 10000 millisecond window
//   feel <interpretation> <feeling>
//                           says how the person felt at a beat this client
//                           was given, once, within five minutes
//   reset feelings          forgets every feeling the person gave
namespace facepalm::protocol {

struct Hello {
  std::string name;
  std::string token;  // Empty when the client showed none.
};
struct ReadRequest {};
struct WaitRequest {
  std::chrono::milliseconds timeout;
};
struct FeelRequest {
  std::int64_t interpretation;
  Feeling feeling;
};
struct ResetRequest {};

struct BeatRequest {
  BeatKind kind;
  std::chrono::milliseconds window;
};

using Request = std::variant<Hello, ReadRequest, WaitRequest, BeatRequest,
                             FeelRequest, ResetRequest>;

// The request on `line`, or nullopt with `error` saying why not.
std::optional<Request> Parse(std::string_view line, std::string& error);

// {"readings":[{"id":..,"summary":"..","detail":{..}}],"missed":n}. A
// reading's detail is JSON facepalm wrote, so it goes in as it is.
// Live readings also carry published_at_ms as Unix epoch milliseconds, and,
// when Config::screen_context_enabled, a context object (#61 item 11):
// what the screen showed at the moment of the reading, also written by
// facepalm and passed through as it is.
// The reply that ends a beat adds
// "interpretation":{"id":..,"feeling":"..","beat":"..","feelings":[..]}:
// the feeling is one of src/feelings.h's names, and "feelings" all of them,
// in order, for a client to offer the person.
// The reply to a request that worked and has nothing to return.
inline constexpr std::string_view kOkJson = R"({"ok":true})";

// Encodes a JSON string, including quotes and control-character escapes.
std::string Quoted(std::string_view text);

std::string DeliveryJson(const Feed::Delivery& delivery);
std::string ErrorJson(std::string_view message);

}  // namespace facepalm::protocol

#endif  // FACEPALM_SRC_PROTOCOL_H_
