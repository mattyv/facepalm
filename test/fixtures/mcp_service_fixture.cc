#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

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
  std::optional<Delivery> Beat(const std::string& client,
                               facepalm::BeatKind kind,
                               std::chrono::milliseconds window) override {
    {
      const std::scoped_lock lock(mutex_);
      beating_ = client;
    }
    {
      const std::scoped_lock lock(output_);
      std::cout << "beat" << '\n' << std::flush;
    }
    auto delivery = feed_.Beat(client, kind, window);
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

}  // namespace

// A deterministic service controlled by stdin, for the cross-process MCP and
// tuning tests: `fixture SOCKET FEELINGS_FILE`. Each "react" line makes a
// nod; "react DETAIL" makes the reaction whose JSON is DETAIL, such as
// {"expression":"brows_down"}.
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char** argv) try {
  if (argc != 3) {
    return 2;
  }
  std::string error;
  const auto feelings = facepalm::FeelingStore::Open(argv[2], error);
  if (!feelings) {
    std::cerr << error << '\n';
    return 1;
  }
  auto listener = facepalm::UnixListener::Create(argv[1], error);
  if (!listener) {
    std::cerr << error << '\n';
    return 1;
  }
  std::mutex output_mutex;
  facepalm::ReactionFeed feed(10, *feelings);
  AnnouncingFeed announcing(feed, output_mutex);
  facepalm::Service service(std::move(listener), announcing);
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
