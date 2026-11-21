#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "src/approved_feed.h"
#include "src/client_approvals.h"
#include "src/client_token.h"
#include "src/feed.h"
#include "src/rating_log.h"
#include "src/reaction_feed.h"
#include "src/service.h"
#include "src/unix_socket.h"

namespace {
// Without --clients, the fixture lets in every client, approved or not.
class EveryClient : public facepalm::Approvals {
 public:
  bool Approved(std::string_view /*token*/) override { return true; }
};
}  // namespace

// A deterministic service controlled by stdin, for the cross-process MCP
// tests: mcp_service_fixture [--clients DIRECTORY] SOCKET [RATINGS]. With
// --clients, it admits clients as the daemon does, through ClientApprovals
// and ApprovedFeed, and prints "camera on" and "camera off" when the camera
// subscription they share changes.
// NOLINTNEXTLINE(misc-const-correctness,readability-function-cognitive-complexity)
int main(int argc, char** argv) try {
  std::span<char*> args(argv + 1, argc - 1);
  std::optional<std::string> clients;
  if (args.size() >= 2 && std::string_view(args[0]) == "--clients") {
    clients = args[1];
    args = args.subspan(2);
  }
  if (args.size() != 1 && args.size() != 2) {
    return 2;
  }
  std::string error;
  auto listener = facepalm::UnixListener::Create(args[0], error);
  if (!listener) {
    std::cerr << error << '\n';
    return 1;
  }
  std::unique_ptr<facepalm::RatingLog> ratings;
  if (args.size() == 2) {
    ratings = facepalm::RatingLog::Open(args[1], error);
    if (!ratings) {
      return 1;
    }
  }
  std::mutex output_mutex;
  facepalm::ReactionFeed feed(10, [&](const auto& rating) {
    if (ratings && !ratings->Record(rating)) {
      return false;
    }
    const std::scoped_lock lock(output_mutex);
    std::cout << "rated " << rating.client << ' ' << rating.id << ' '
              << rating.score << '\n'
              << std::flush;
    return true;
  });
  const std::unique_ptr<facepalm::ApprovalsIo> io =
      facepalm::PosixApprovalsIo();
  facepalm::ClientApprovals approved_clients(
      clients.value_or(""),
      [](const std::string& path) { return facepalm::LoadClientToken(path); },
      *io);
  EveryClient every_client;
  facepalm::ApprovedFeed approved_feed(
      feed,
      [&](bool joined) {
        const std::scoped_lock lock(output_mutex);
        std::cout << (joined ? "camera on" : "camera off") << '\n'
                  << std::flush;
      },
      [&](const std::string& session) { feed.DisconnectAndForget(session); });
  facepalm::Feed& served =
      clients ? static_cast<facepalm::Feed&>(approved_feed) : feed;
  facepalm::Approvals& approvals =
      clients ? static_cast<facepalm::Approvals&>(approved_clients)
              : every_client;
  facepalm::Service service(std::move(listener), served, approvals);
  service.Start();
  std::cout << "ready" << '\n' << std::flush;
  std::string command;
  while (std::getline(std::cin, command)) {
    if (command != "react") {
      return 2;
    }
    constexpr std::int64_t kSyntheticPublishedMs = 1763658000123;
    const auto id = feed.React("synthetic nod", R"({"gesture":"nod"})",
                               kSyntheticPublishedMs);
    const std::scoped_lock lock(output_mutex);
    std::cout << "reacted " << id << '\n' << std::flush;
  }
  service.Stop();
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
