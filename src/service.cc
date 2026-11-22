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
      if (!Answer(connection, *name, *line)) {
        break;
      }
    }
    // Service!Closed: the client leaves the feed (GuardLeaveOnClose),
    // through Leave, which changes nothing if the watcher's
    // Service!Noticed or Stop's BeginStop already did (#115). Its name is
    // cleared first, so a fresh BeginStop never finds it to disconnect
    // again either.
    {
      const std::scoped_lock lock(mutex_);
      connection.name.reset();
    }
    Leave(connection, *name);
  }
  // specs/Service.tla's Closed, GuardCalibCancelOnClose (#128): before
  // this connection's slot can be freed and reused, any live calibration
  // session it alone still owns is cancelled, never left for a later,
  // unrelated connection to find still running.
  LeaveCalibration(connection);
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
    // Inlined, not Leave: this already holds mutex_.
    if (connection.name && !connection.left) {
      connection.left = true;
      feed_.Disconnect(*connection.name);
    }
  }
  return true;
}

bool Service::Answer(Connection& connection, const std::string& name,
                     std::string_view line) {
  Stream& stream = *connection.stream;
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
    stream.WriteLine(CalibrateReply(connection, *calibrate));
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
    delivery = Watched(connection, name, [&] {
      return feed_.Beat(name, beat->kind, beat->window, beat->content);
    });
  } else if (const auto* wait = std::get_if<protocol::WaitRequest>(&*request)) {
    delivery = Watched(connection, name,
                       [&] { return feed_.Wait(name, wait->timeout); });
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
// nothing, a watcher leaves the feed when the peer hangs up, through
// Leave, which ends the wait at once (GuardHangUpWakes) and is the one
// Service!Closed effect on the feed for this connection, whether the
// watcher, Stop or Serve's own cleanup gets there first (#115).
std::optional<Feed::Delivery> Service::Watched(
    Connection& connection, const std::string& name,
    const std::function<std::optional<Feed::Delivery>()>& wait) {
  Stream& stream = *connection.stream;
  std::thread watcher([&] {
    if (stream.AwaitHangUp()) {
      Leave(connection, name);
    }
  });
  std::optional<Feed::Delivery> delivery = wait();
  stream.EndAwaitHangUp();
  watcher.join();
  return delivery;
}

// The one Service!Closed effect on the feed for `connection`: changes
// nothing once another caller, the watcher's Service!Noticed, Stop's
// BeginStop or this same cleanup from somewhere else, has already left it,
// so a name freed here and taken by a new connection is never disconnected
// a second time (#115, GuardLeaveOnClose).
void Service::Leave(Connection& connection, const std::string& name) {
  std::unique_lock lock(mutex_);
  if (connection.left) {
    return;
  }
  connection.left = true;
  lock.unlock();
  feed_.Disconnect(name);
}

// specs/Calibration.tla's Start (specs/Service.tla's CalibrateStart):
// ties the session, once it starts, to `connection`, so its own
// disconnect can find it again in LeaveCalibration below (#128).
std::string Service::CalibrateStartReply(Connection& connection, int screens,
                                         std::string& error) {
  const std::optional<CalibrationController::Status> status =
      calibration_->Start(screens, error);
  if (status) {
    const std::scoped_lock lock(mutex_);
    calibration_owner_ = &connection;
  }
  return CalibrationStatusOrError(status, error);
}

// specs/Calibration.tla's Advance/Finish (specs/Service.tla's
// CalibrateEnds, once the last step is finished): any connected client
// may drive the one session, not only the one CalibrateStartReply tied
// it to, so finishing it clears that tie no matter who asked (#128).
std::string Service::CalibrateNextReply(std::string& error) {
  const std::optional<CalibrationController::Status> status =
      calibration_->Next(error);
  if (status && status->done) {
    const std::scoped_lock lock(mutex_);
    calibration_owner_ = nullptr;
  }
  return CalibrationStatusOrError(status, error);
}

// specs/Calibration.tla's Cancel (specs/Service.tla's CalibrateEnds):
// the same as CalibrateNextReply's own tie-clearing, from whichever
// connection cancels it.
std::string Service::CalibrateCancelReply(std::string& error) {
  const bool ok = calibration_->Cancel(error);
  if (ok) {
    const std::scoped_lock lock(mutex_);
    calibration_owner_ = nullptr;
  }
  return ok ? std::string(protocol::kOkJson) : protocol::ErrorJson(error);
}

// The reply to a calibrate request, whether or not calibration_ is set.
// A calibrate request needs no client identity to be answered: any
// connected client may drive the one session (CalibrateNextReply,
// CalibrateCancelReply); only starting it ties it to `connection`
// (CalibrateStartReply).
std::string Service::CalibrateReply(Connection& connection,
                                    const protocol::CalibrateRequest& request) {
  if (calibration_ == nullptr) {
    return protocol::ErrorJson("calibration is not available");
  }
  using Action = protocol::CalibrateRequest::Action;
  std::string error;
  switch (request.action) {
    case Action::kStart:
      return CalibrateStartReply(connection, request.screens, error);
    case Action::kNext:
      return CalibrateNextReply(error);
    default:  // Action::kCancel.
      return CalibrateCancelReply(error);
  }
}

void Service::LeaveCalibration(Connection& connection) {
  bool owns_it = false;
  {
    const std::scoped_lock lock(mutex_);
    owns_it = calibration_owner_ == &connection;
    if (owns_it) {
      calibration_owner_ = nullptr;
    }
  }
  // calibration_owner_ is only ever set in CalibrateStartReply, reached
  // only when calibration_ is not null, so owns_it being true already
  // means this connection's "calibrate start" went to a real calibration_;
  // a second null check here is code no run of this service can reach.
  if (owns_it) {
    std::string error;
    calibration_->Cancel(error);
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
