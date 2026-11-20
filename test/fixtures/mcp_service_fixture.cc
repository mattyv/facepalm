#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

#include "src/rating_log.h"
#include "src/reaction_feed.h"
#include "src/service.h"
#include "src/unix_socket.h"

// A deterministic service controlled by stdin, for the cross-process MCP test.
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char** argv) try {
  if (argc != 2 && argc != 3) {
    return 2;
  }
  std::string error;
  auto listener = facepalm::UnixListener::Create(argv[1], error);
  if (!listener) {
    std::cerr << error << '\n';
    return 1;
  }
  std::unique_ptr<facepalm::RatingLog> ratings;
  if (argc == 3) {
    ratings = facepalm::RatingLog::Open(argv[2], error);
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
  facepalm::Service service(std::move(listener), feed);
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
