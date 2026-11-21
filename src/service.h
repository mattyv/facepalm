#ifndef FACEPALM_SRC_SERVICE_H_
#define FACEPALM_SRC_SERVICE_H_

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "src/feed.h"

namespace facepalm {

// One connection's line-by-line byte stream.
class Stream {
 public:
  virtual ~Stream() = default;
  // The next line, without its newline; nullopt once the peer hangs up or
  // the stream is shut down.
  virtual std::optional<std::string> ReadLine() = 0;
  virtual void WriteLine(std::string_view line) = 0;
  // Ends a ReadLine blocked in another thread, and every one after it.
  virtual void Shutdown() = 0;
  // Blocks while the connection's thread waits, until the peer hangs up or
  // the stream is shut down (true), or until EndAwaitHangUp, or the peer
  // sends something, which stays to be read (false). Call it from one
  // thread, and EndAwaitHangUp once for each call, from another.
  virtual bool AwaitHangUp() = 0;
  // Ends the AwaitHangUp under way, or the next one if none has started.
  virtual void EndAwaitHangUp() = 0;
};

// Where connections come from: a socket only its user can open.
class Listener {
 public:
  virtual ~Listener() = default;
  // The next connection; nullptr once closed.
  virtual std::unique_ptr<Stream> Accept() = 0;
  // Ends an Accept blocked in another thread, and every one after it.
  virtual void Close() = 0;
};

// Which tokens the person approved (#37).
class Approvals {
 public:
  virtual ~Approvals() = default;
  // Whether `token` is one the person approved; "" when a client shows none.
  [[nodiscard]] virtual bool Approved(std::string_view token) = 0;
};

// facepalm's background service (#37), following specs/Service.tla. Each
// connection gets its own thread, which speaks src/protocol.h to the feed
// for the client the connection names. Only a client showing an approved
// token is let in; several connections may show the same one.
class Service {
 public:
  Service(std::unique_ptr<Listener> listener, Feed& feed, Approvals& approvals);
  ~Service();
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;

  // Accepts connections on a thread of its own.
  void Start();
  // Service!Stop, then Service!Stopped: returns once every connection's
  // thread has ended.
  void Stop();

 private:
  struct Connection {
    std::unique_ptr<Stream> stream;
    std::optional<std::string> name;  // Once the client has said hello.
    bool done = false;
    std::thread thread;
  };

  void AcceptAll();
  void Serve(Connection& connection);
  // The client's name once it has said hello, or nullopt to end.
  std::optional<std::string> Greet(Connection& connection);
  // Records the client's name; false once stopping.
  bool Name(Connection& connection, const std::string& name);
  // Marks the service stopping, shuts every connection and disconnects every
  // named client; false when it already was.
  bool BeginStop();
  // The reply to one request from a named client; false to end.
  bool Answer(const std::string& name, std::string_view line, Stream& stream);
  // Runs `wait`, a wait or a beat, while watching for a hang-up.
  std::optional<Feed::Delivery> Watched(
      const std::string& name, Stream& stream,
      const std::function<std::optional<Feed::Delivery>()>& wait);

  std::unique_ptr<Listener> listener_;
  Feed& feed_;
  Approvals& approvals_;
  std::mutex mutex_;
  bool stopping_ = false;
  std::list<Connection> connections_;
  std::thread acceptor_;
};

}  // namespace facepalm

#endif  // FACEPALM_SRC_SERVICE_H_
