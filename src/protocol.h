#ifndef FACEPALM_SRC_PROTOCOL_H_
#define FACEPALM_SRC_PROTOCOL_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "src/calibration.h"
#include "src/camera_directory.h"
#include "src/feed.h"
#include "src/feelings.h"
#include "src/screen_context.h"

// What a client and facepalm's service say to each other (#37): a request
// is one line of text, and each reply is one line of JSON.
//
//   hello <name> [<token>]  names the client and shows its approval token;
//                           the first line of a connection
//   read                    the readings since this client last looked
//   wait <milliseconds>     as read, waiting up to 60 seconds for one
//   beat <kind> <milliseconds> [<x> <y> <width> <height>]
//                           marks a beat after a question or a proposal, and
//                           waits out its 1 to 10000 millisecond window; the
//                           optional rectangle (#144) is a content area on
//                           screen, in whatever coordinate space the
//                           platform source reports (src/screen_context.h's
//                           ScreenRect), for the reply's own content field
//   feel <interpretation> <feeling>
//                           says how the person felt at a beat this client
//                           was given, once, within five minutes
//   reset feelings          forgets every feeling the person gave
//   calibrate start [screens]|next|cancel
//                           starts, steps through or cancels an optional
//                           calibration session (#36, #35, item 12 of #61)
//   cameras                 the cameras available now, for Settings to
//                           offer (item 10 of #61, #13)
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
  // The content rectangle a client may give alongside a beat (#144).
  std::optional<ContentRect> content = std::nullopt;
};

struct CalibrateRequest {
  enum class Action : std::uint8_t { kStart, kNext, kCancel };
  Action action;
  int screens = 1;  // Only meaningful for kStart.
};

struct CamerasRequest {};

using Request =
    std::variant<Hello, ReadRequest, WaitRequest, BeatRequest, FeelRequest,
                 ResetRequest, CalibrateRequest, CamerasRequest>;

// The request on `line`, or nullopt with `error` saying why not.
std::optional<Request> Parse(std::string_view line, std::string& error);

// {"readings":[{"id":..,"summary":"..","detail":{..}}],"missed":n}. A
// reading's detail is JSON facepalm wrote, so it goes in as it is.
// Live readings also carry published_at_ms as Unix epoch milliseconds, and,
// when Config::screen_context_enabled, a context object (#61 item 11):
// what the screen showed at the moment of the reading, also written by
// facepalm and passed through as it is.
// The reply that ends a beat adds
// "interpretation":{"id":..,"feeling":"..","beat":"..","gaze":"..",
// "feelings":[..]}, and, when the beat carried a content rectangle (#144),
// "content":".." too: the feeling is one of src/feelings.h's names,
// "feelings" all of them, in order, for a client to offer the person, gaze
// is one of on_content, averted, away or unknown (#142,
// src/screen_context.h's BeatGazeName; context only, never a rule's input
// for feeling, the one exception being a question beat that ends averted,
// which reads unknown), and content, when present, is yes, no or unknown
// (src/screen_context.h's ContentHit).
// The reply to a request that worked and has nothing to return.
inline constexpr std::string_view kOkJson = R"({"ok":true})";

// Encodes a JSON string, including quotes and control-character escapes.
std::string Quoted(std::string_view text);

std::string DeliveryJson(const Feed::Delivery& delivery);
std::string ErrorJson(std::string_view message);

// {"ok":true,"calibration":{"state":"running","index":i,"total":n,
// "step":"resting"}}, "step":"gains" for #137's fixate-and-turn step, or
// "step":"screen","screen":s,"point":".." for a screen's point; any of
// these three also carries "refused":".." when the Next() just answered
// was refused (#177: no pose captured yet, #137's weak-step checks, or a
// failed save) -- the step named is the same one being retried, not a
// new one, and the session is still running, unlike an {"error":..}
// reply, which ends it. {"ok":true,"calibration":{"state":"done",
// "screens":n}} once the last step is finished and saved, naming the
// screens just calibrated (#135, #137).
std::string CalibrationStatusJson(const CalibrationController::Status& status);

// {"ok":true,"cameras":[{"id":"..","name":"..","built_in":true}]} (item 10
// of #61, #13).
std::string CamerasJson(const std::vector<CameraDevice>& devices);

}  // namespace facepalm::protocol

#endif  // FACEPALM_SRC_PROTOCOL_H_
