#include "src/calibration_flow.h"

#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "src/calibration.h"

namespace facepalm {
namespace {

// protocol::ErrorJson (src/protocol.cc) always starts a reply this way.
constexpr std::string_view kErrorPrefix = R"({"error":)";
// The client name both calibrate_cli and the menu show the service;
// approved alike (facepalm-daemon approve calibrate), so the menu reuses
// the same token calibrate_cli would, rather than a client name of its
// own.
constexpr std::string_view kClientName = "calibrate";

// The string value of `"key":"value"` in `json`, or nullopt. `json` and
// `key` are never swappable in practice -- every call site below passes a
// literal key -- so the mistake this warns about cannot happen here.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<std::string> StringField(std::string_view json,
                                       std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":\"";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t value_start = start + needle.size();
  const std::size_t value_end = json.find('"', value_start);
  if (value_end == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(json.substr(value_start, value_end - value_start));
}

// The integer value of `"key":N` in `json`, or nullopt. See StringField
// above: `json` and `key` are never swappable in practice.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<int> IntField(std::string_view json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t digits_start = start + needle.size();
  int value = 0;
  // from_chars sets ec to errc::invalid_argument whenever it consumes no
  // characters, so that case needs no separate check of its own.
  const std::from_chars_result result = std::from_chars(
      json.data() + digits_start, json.data() + json.size(), value);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

// Parses one calibrate reply line the way protocol::CalibrationStatusJson
// (src/protocol.cc) built it, into the Status it was built from: a bare
// `{"ok":true}` (hello's or a cancel's own reply, which never carries a
// "calibration" object) and `{"calibration":{"state":"idle"}}` both parse
// as Status{running=false, done=false}, exactly as the daemon's own
// "nothing more to say" replies. nullopt, with `error`, for `{"error":...}`
// or any reply this cannot make sense of.
std::optional<CalibrationController::Status> ParseCalibrationReply(
    std::string_view line, std::string& error) {
  if (line.starts_with(kErrorPrefix)) {
    error = StringField(line, "error").value_or(std::string(line));
    return std::nullopt;
  }
  CalibrationController::Status status;
  const std::string state = StringField(line, "state").value_or(std::string());
  if (state == "done") {
    status.done = true;
    return status;
  }
  if (state != "running") {
    // "idle", or no "calibration" object at all: nothing more to report.
    return status;
  }
  status.running = true;
  status.index = IntField(line, "index").value_or(0);
  status.total = IntField(line, "total").value_or(0);
  const std::string step = StringField(line, "step").value_or(std::string());
  if (step == "resting") {
    status.resting_step = true;
    return status;
  }
  status.screen = IntField(line, "screen").value_or(0);
  status.point =
      CalibrationPointNamed(StringField(line, "point").value_or(std::string()))
          .value_or(CalibrationPoint::kCentre);
  return status;
}

}  // namespace

CalibrationFlow::CalibrationFlow(std::string socket_path, std::string token,
                                 CalibrateClientIo& io)
    : socket_path_(std::move(socket_path)), token_(std::move(token)), io_(io) {}

void CalibrationFlow::Fail(std::string error) {
  view_ = View{.state = State::kFailed, .error = std::move(error)};
}

void CalibrationFlow::ApplyReply(const std::optional<std::string>& reply) {
  if (!reply) {
    Fail("connection closed before a reply arrived");
    return;
  }
  std::string error;
  const std::optional<CalibrationController::Status> status =
      ParseCalibrationReply(*reply, error);
  if (!status) {
    Fail(error);
    return;
  }
  if (status->done) {
    view_ = View{.state = State::kDone, .error = {}};
    return;
  }
  if (!status->running) {
    Fail("the daemon ended the session unexpectedly");
    return;
  }
  view_ = View{.state = State::kPrompting,
               .resting_step = status->resting_step,
               .screen = status->screen,
               .point = status->point,
               .index = status->index,
               .total = status->total,
               .error = {}};
}

void CalibrationFlow::Start(int screens) {
  if (!io_.Connect(socket_path_)) {
    Fail("cannot connect to " + socket_path_);
    return;
  }
  std::string hello_line = "hello " + std::string(kClientName);
  if (!token_.empty()) {
    hello_line += " " + token_;
  }
  io_.WriteLine(hello_line);
  const std::optional<std::string> hello = io_.ReadLine();
  if (!hello) {
    Fail("connection closed before a reply arrived");
    return;
  }
  std::string error;
  if (!ParseCalibrationReply(*hello, error)) {
    Fail(error);
    return;
  }
  io_.WriteLine("calibrate start " + std::to_string(screens));
  ApplyReply(io_.ReadLine());
}

void CalibrationFlow::Next() {
  if (view_.state != State::kPrompting) {
    return;
  }
  io_.WriteLine("calibrate next");
  ApplyReply(io_.ReadLine());
}

void CalibrationFlow::SendCancelAndClose() {
  io_.WriteLine("calibrate cancel");
  const std::optional<std::string> reply = io_.ReadLine();
  if (!reply) {
    Fail("connection closed before a reply arrived");
    return;
  }
  std::string error;
  if (!ParseCalibrationReply(*reply, error)) {
    Fail(error);
    return;
  }
  view_ = View{};
}

void CalibrationFlow::Cancel() {
  if (view_.state != State::kPrompting) {
    return;
  }
  SendCancelAndClose();
}

void CalibrationFlow::WindowClosed() { Cancel(); }

}  // namespace facepalm
