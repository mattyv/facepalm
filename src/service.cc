#include "src/service.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "src/camera_directory.h"
#include "src/feed.h"
#include "src/protocol.h"

namespace facepalm {
namespace {

// `status`'s JSON, or `error`'s when there is none.
std::string CalibrationStatusOrError(
    const std::optional<CalibrationController::Status>& status,
    const std::string& error) {
  return status ? protocol::CalibrationStatusJson(*status)
                : protocol::ErrorJson(error);
}

}  // namespace

Service::Service(std::unique_ptr<Listener> listener, Feed& feed,
                 Approvals& approvals, CalibrationController* calibration,
                 CameraDirectory* cameras)
    : listener_(std::move(listener)),
      feed_(feed),
      approvals_(approvals),
      calibration_(calibration),
      cameras_(cameras) {}

Service::~Service() { Stop(); }

void Service::Start() {
  acceptor_ = std::thread([this] { AcceptAll(); });
}

// Service!Accept, while running; Service!Stop closes the listener.
void Service::AcceptAll() {
  while (std::unique_ptr<Stream> stream = listener_->Accept()) {
    const std::scoped_lock lock(mutex_);
    std::erase_if(connections_, [](Connection& connection) {
      if (connection.done) {
        connection.thread.join();
      }
      return connection.done;
    });
    if (stopping_) {
      stream->Shutdown();
      continue;
    }
    Connection& connection = connections_.emplace_back();
    connection.stream = std::move(stream);
    connection.thread = std::thread([this, &connection] { Serve(connection); });
  }
}

// One connection's thread, from its hello to Service!Closed.
void Service::Serve(Connection& connection) {
  const std::optional<std::string> name = Greet(connection);
  if (name) {
    while (const std::optional<std::string> line =
               connection.stream->ReadLine()) {
      if (!Answer(*name, *line, *connection.stream)) {
        break;
      }
    }
    // Service!Closed: the client leaves the feed (GuardLeaveOnClose). Its
    // name is cleared first, so Stop does not disconnect it again; if Stop
    // got there first, the second Disconnect changes nothing.
    {
      const std::scoped_lock lock(mutex_);
      connection.name.reset();
    }
    feed_.Disconnect(*name);
  }
  const std::scoped_lock lock(mutex_);
  connection.done = true;
}

// Service!Hello: refuses a token not approved (GuardApproval), then a name
// already connected (GuardUniqueNames).
std::optional<std::string> Service::Greet(Connection& connection) {
  Stream& stream = *connection.stream;
  const std::optional<std::string> line = stream.ReadLine();
  if (!line) {
    return std::nullopt;
  }
  std::string error;
  const std::optional<protocol::Request> request =
      protocol::Parse(*line, error);
  if (!request) {
    stream.WriteLine(protocol::ErrorJson(error));
    return std::nullopt;
  }
  const auto* hello = std::get_if<protocol::Hello>(&*request);
  if (hello == nullptr) {
    stream.WriteLine(protocol::ErrorJson("say hello first"));
    return std::nullopt;
  }
  if (!approvals_.Approved(hello->token)) {
    stream.WriteLine(protocol::ErrorJson("client not approved"));
    return std::nullopt;
  }
  if (!feed_.Connect(hello->name)) {
    stream.WriteLine(protocol::ErrorJson("client not accepted"));
    return std::nullopt;
  }
  if (!Name(connection, hello->name)) {
    // Stop has already disconnected every named client; this one too.
    feed_.Disconnect(hello->name);
    return std::nullopt;
  }
  stream.WriteLine(protocol::kOkJson);
  return hello->name;
}

bool Service::Name(Connection& connection, const std::string& name) {
  const std::scoped_lock lock(mutex_);
  if (stopping_) {
    return false;
  }
  connection.name = name;
  return true;
}

bool Service::BeginStop() {
  const std::scoped_lock lock(mutex_);
  if (stopping_) {
    return false;
  }
  stopping_ = true;
  for (Connection& connection : connections_) {
    connection.stream->Shutdown();
    if (connection.name) {
      feed_.Disconnect(*connection.name);
    }
  }
  return true;
}

bool Service::Answer(const std::string& name, std::string_view line,
                     Stream& stream) {
  std::string error;
  const std::optional<protocol::Request> request = protocol::Parse(line, error);
  if (!request) {
    stream.WriteLine(protocol::ErrorJson(error));
    return true;
  }
  if (std::holds_alternative<protocol::Hello>(*request)) {
    stream.WriteLine(protocol::ErrorJson("already said hello"));
    return true;
  }
  // specs/FeelingFeedback.tla's Feel and Reset, answered once the feed has
  // saved them, or refused.
  if (const auto* feel = std::get_if<protocol::FeelRequest>(&*request)) {
    stream.WriteLine(feed_.Feel(name, feel->interpretation, feel->feeling)
                         ? std::string(protocol::kOkJson)
                         : protocol::ErrorJson("cannot take that feeling"));
    return true;
  }
  if (std::holds_alternative<protocol::ResetRequest>(*request)) {
    stream.WriteLine(feed_.Reset(name)
                         ? std::string(protocol::kOkJson)
                         : protocol::ErrorJson("cannot reset feelings"));
    return true;
  }
  if (const auto* calibrate =
          std::get_if<protocol::CalibrateRequest>(&*request)) {
    stream.WriteLine(CalibrateReply(*calibrate));
    return true;
  }
  // The cameras available now (item 10 of #61, #13), independent of the
  // one in use; no cameras when the service is not given a directory.
  if (std::holds_alternative<protocol::CamerasRequest>(*request)) {
    stream.WriteLine(protocol::CamerasJson(
        cameras_ == nullptr ? std::vector<CameraDevice>() : cameras_->List()));
    return true;
  }
  // Service!Wait and Service!Answered, for a wait or a beat
  // (specs/FeelingBeat.tla), or a read.
  std::optional<Feed::Delivery> delivery;
  if (const auto* beat = std::get_if<protocol::BeatRequest>(&*request)) {
    delivery = Watched(name, stream, [&] {
      return feed_.Beat(name, beat->kind, beat->window);
    });
  } else if (const auto* wait = std::get_if<protocol::WaitRequest>(&*request)) {
    delivery =
        Watched(name, stream, [&] { return feed_.Wait(name, wait->timeout); });
  } else {
    delivery = feed_.Read(name);
  }
  if (!delivery) {
    // Only Stop disconnects a client while its connection is open.
    stream.WriteLine(protocol::ErrorJson("closing"));
    return false;
  }
  stream.WriteLine(protocol::DeliveryJson(*delivery));
  return true;
}

// Service!Noticed: while the connection's thread waits, and so reads
// nothing, a watcher disconnects the client when the peer hangs up, which
// ends the wait at once (GuardHangUpWakes).
std::optional<Feed::Delivery> Service::Watched(
    const std::string& name, Stream& stream,
    const std::function<std::optional<Feed::Delivery>()>& wait) {
  std::thread watcher([&] {
    if (stream.AwaitHangUp()) {
      feed_.Disconnect(name);
    }
  });
  std::optional<Feed::Delivery> delivery = wait();
  stream.EndAwaitHangUp();
  watcher.join();
  return delivery;
}

// specs/Calibration.tla's Start, Advance, Finish and Cancel, dispatched to
// calibration_ (item 12 of #61). A calibrate request needs no client
// identity: any connected client may drive the one session.
std::string Service::CalibrateReply(const protocol::CalibrateRequest& request) {
  if (calibration_ == nullptr) {
    return protocol::ErrorJson("calibration is not available");
  }
  using Action = protocol::CalibrateRequest::Action;
  std::string error;
  switch (request.action) {
    case Action::kStart:
      return CalibrationStatusOrError(
          calibration_->Start(request.screens, error), error);
    case Action::kNext:
      return CalibrationStatusOrError(calibration_->Next(error), error);
    default:  // Action::kCancel.
      return calibration_->Cancel(error) ? std::string(protocol::kOkJson)
                                         : protocol::ErrorJson(error);
  }
}

// Service!Stop: refuses new connections (GuardRefuseWhenStopping), shuts
// every connection, and disconnects every client, which ends its wait
// (GuardStopWakes). Then Service!Stopped, once every thread has ended.
void Service::Stop() {
  if (!BeginStop()) {
    return;
  }
  listener_->Close();
  if (acceptor_.joinable()) {
    acceptor_.join();
  }
  for (Connection& connection : connections_) {
    connection.thread.join();
  }
}

}  // namespace facepalm
