#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "src/approved_feed.h"
#include "src/client_approvals.h"
#include "src/client_token.h"
#include "src/feed.h"
#include "src/feeling_store.h"
#include "src/feelings.h"
#include "src/reaction_feed.h"
#include "src/service.h"
#include "src/unix_socket.h"

namespace {

// Passes every request to the feed, and prints "beat" as a beat begins, so
// the test can make its reading inside the beat's window.
class AnnouncingFeed : public facepalm::Feed {
 public:
  AnnouncingFeed(facepalm::ReactionFeed& feed, std::mutex& output)
      : feed_(feed), output_(output) {}

  bool Connect(const std::string& client) override {
    return feed_.Connect(client);
  }
  void Disconnect(const std::string& client) override {
    feed_.Disconnect(client);
  }
  std::optional<Delivery> Read(const std::string& client) override {
    return feed_.Read(client);
  }
  std::optional<Delivery> Wait(const std::string& client,
                               std::chrono::milliseconds timeout) override {
    return feed_.Wait(client, timeout);
  }
  std::optional<Delivery> Beat(
      const std::string& client, facepalm::BeatKind kind,
      std::chrono::milliseconds window,
      std::optional<facepalm::ContentRect> content) override {
    {
      const std::scoped_lock lock(mutex_);
      beating_ = client;
    }
    {
      const std::scoped_lock lock(output_);
      std::cout << "beat" << '\n' << std::flush;
    }
    auto delivery = feed_.Beat(client, kind, window, content);
    const std::scoped_lock lock(mutex_);
    beating_.clear();
    return delivery;
  }
  bool Feel(const std::string& client, std::int64_t interpretation,
            facepalm::Feeling feeling) override {
    return feed_.Feel(client, interpretation, feeling);
  }
  bool Reset(const std::string& client) override { return feed_.Reset(client); }

  // Returns once the beat announced, if any, has opened its window.
  void AwaitBeat() {
    const std::string client = Beating();
    while (!client.empty() && !feed_.Waiting(client) && Beating() == client) {
      std::this_thread::yield();
    }
  }

 private:
  std::string Beating() {
    const std::scoped_lock lock(mutex_);
    return beating_;
  }

  facepalm::ReactionFeed& feed_;
  std::mutex& output_;
  std::mutex mutex_;
  std::string beating_;  // Under mutex_; the client in a beat, if any.
};

// Without --clients, the fixture lets in every client, approved or not.
class EveryClient : public facepalm::Approvals {
 public:
  bool Approved(std::string_view /*token*/) override { return true; }
};

}  // namespace

// A deterministic service controlled by stdin, for the cross-process MCP and
// tuning tests: `fixture [--clients DIRECTORY] SOCKET FEELINGS_FILE`. Each
// "react" line makes a nod; "react DETAIL" makes the reaction whose JSON is
// DETAIL, such as {"expression":"brows_down"}. With --clients, it admits
// clients as the daemon does, through ClientApprovals and ApprovedFeed, and
// prints "camera on" and "camera off" when the camera subscription they
// share changes.
// NOLINTNEXTLINE(misc-const-correctness,readability-function-cognitive-complexity)
int main(int argc, char** argv) try {
  std::span<char*> args(argv + 1, argc - 1);
  std::optional<std::string> clients;
  if (args.size() >= 2 && std::string_view(args[0]) == "--clients") {
    clients = args[1];
    args = args.subspan(2);
  }
  if (args.size() != 2) {
    return 2;
  }
  std::string error;
  const auto feelings = facepalm::FeelingStore::Open(args[1], error);
  if (!feelings) {
    std::cerr << error << '\n';
    return 1;
  }
  auto listener = facepalm::UnixListener::Create(args[0], error);
  if (!listener) {
    std::cerr << error << '\n';
    return 1;
  }
  std::mutex output_mutex;
  facepalm::ReactionFeed feed(10, *feelings);
  AnnouncingFeed announcing(feed, output_mutex);
  const std::unique_ptr<facepalm::ApprovalsIo> io =
      facepalm::PosixApprovalsIo();
  facepalm::ClientApprovals approved_clients(
      clients.value_or(""),
      [](const std::string& path) { return facepalm::LoadClientToken(path); },
      *io);
  EveryClient every_client;
  facepalm::ApprovedFeed approved_feed(
      announcing,
      [&](bool joined) {
        const std::scoped_lock lock(output_mutex);
        std::cout << (joined ? "camera on" : "camera off") << '\n'
                  << std::flush;
      },
      [&](const std::string& session) { feed.DisconnectAndForget(session); });
  facepalm::Feed& served =
      clients ? static_cast<facepalm::Feed&>(approved_feed) : announcing;
  facepalm::Approvals& approvals =
      clients ? static_cast<facepalm::Approvals&>(approved_clients)
              : every_client;
  facepalm::Service service(std::move(listener), served, approvals);
  service.Start();
  std::cout << "ready" << '\n' << std::flush;
  std::string command;
  constexpr std::string_view kReact = "react";
  while (std::getline(std::cin, command)) {
    if (!command.starts_with(kReact)) {
      return 2;
    }
    const std::string detail = command == kReact
                                   ? std::string(R"({"gesture":"nod"})")
                                   : command.substr(kReact.size() + 1);
    announcing.AwaitBeat();
    constexpr std::int64_t kSyntheticPublishedMs = 1763658000123;
    const auto id =
        feed.React("synthetic " + std::string(facepalm::SignOf(detail)), detail,
                   kSyntheticPublishedMs);
    const std::scoped_lock lock(output_mutex);
    std::cout << "reacted " << id << '\n' << std::flush;
  }
  service.Stop();
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
