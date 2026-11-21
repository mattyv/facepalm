#include "src/service.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include "src/feed.h"
#include "src/protocol.h"

namespace facepalm {

Service::Service(std::unique_ptr<Listener> listener, Feed& feed)
    : listener_(std::move(listener)), feed_(feed) {}

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

// Service!Hello: refuses a name already connected (GuardUniqueNames).
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
  if (const auto* rate = std::get_if<protocol::RateRequest>(&*request)) {
    stream.WriteLine(feed_.Rate(name, rate->id, rate->score)
                         ? std::string(protocol::kOkJson)
                         : protocol::ErrorJson("cannot rate that reading"));
    return true;
  }
  // Service!Wait and Service!Answered, for a wait or a beat
  // (specs/FeelingBeat.tla), or a read.
  std::optional<Feed::Delivery> delivery;
  if (const auto* beat = std::get_if<protocol::BeatRequest>(&*request)) {
    delivery = feed_.Beat(name, beat->kind, beat->window);
  } else if (const auto* wait = std::get_if<protocol::WaitRequest>(&*request)) {
    delivery = feed_.Wait(name, wait->timeout);
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
