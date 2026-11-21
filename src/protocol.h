#ifndef FACEPALM_SRC_PROTOCOL_H_
#define FACEPALM_SRC_PROTOCOL_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "src/calibration.h"
#include "src/feed.h"
#include "src/feelings.h"

// What a client and facepalm's service say to each other (#37): a request
// is one line of text, and each reply is one line of JSON.
//
//   hello <name>          names the client; the first line of a connection
//   read                  the readings since this client last looked
//   wait <milliseconds>   as read, waiting up to 60 seconds for one
//   rate <id> <score>     rates a reading this client was shown, 1 to 5
//   beat <kind> <milliseconds>
//                         marks a beat after a question or a proposal, and
//                         waits out its 1 to 10000 millisecond window
//   calibrate start [screens]|next|cancel
//                         starts, steps through or cancels an optional
//                         calibration session (#36, #35, item 12 of #61)
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

struct BeatRequest {
  BeatKind kind;
  std::chrono::milliseconds window;
};

struct CalibrateRequest {
  enum class Action : std::uint8_t { kStart, kNext, kCancel };
  Action action;
  int screens = 1;  // Only meaningful for kStart.
};

using Request = std::variant<Hello, ReadRequest, WaitRequest, RateRequest,
                             BeatRequest, CalibrateRequest>;

// The request on `line`, or nullopt with `error` saying why not.
std::optional<Request> Parse(std::string_view line, std::string& error);

// {"readings":[{"id":..,"summary":"..","detail":{..}}],"missed":n}. A
// reading's detail is JSON facepalm wrote, so it goes in as it is.
// Live readings also carry published_at_ms as Unix epoch milliseconds.
// The reply that ends a beat adds
// "interpretation":{"id":..,"feeling":"..","beat":".."}, with the feeling
// one of src/feelings.h's names.
// The reply to a request that worked and has nothing to return.
inline constexpr std::string_view kOkJson = R"({"ok":true})";

// Encodes a JSON string, including quotes and control-character escapes.
std::string Quoted(std::string_view text);

std::string DeliveryJson(const Feed::Delivery& delivery);
std::string ErrorJson(std::string_view message);

// {"ok":true,"calibration":{"state":"running","index":i,"total":n,
// "step":"resting"}}, or with "step":"screen","screen":s,"point":".." for a
// screen's point; {"ok":true,"calibration":{"state":"done"}} once the last
// step is finished and saved.
std::string CalibrationStatusJson(const CalibrationController::Status& status);

}  // namespace facepalm::protocol

#endif  // FACEPALM_SRC_PROTOCOL_H_
