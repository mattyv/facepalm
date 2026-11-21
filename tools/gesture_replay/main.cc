#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

#include "src/gesture_replay.h"
#include "src/look_away_replay.h"

// Replays a camera_probe log from standard input: its head gestures, or,
// given --looking-away, its looks away (#35).
int main(int argc, char** argv) {
  const std::span<char*> args(argv, static_cast<std::size_t>(argc));
  const bool looking_away =
      args.size() == 2 && std::string_view(args[1]) == "--looking-away";
  if (args.size() > 2 || (args.size() == 2 && !looking_away)) {
    std::cerr << "usage: gesture_replay [--looking-away] < camera-probe.log\n";
    return 2;
  }
  const bool replayed = looking_away
                            ? facepalm::ReplayLookingAway(std::cin, std::cout)
                            : facepalm::ReplayGestures(std::cin, std::cout);
  if (!replayed) {
    std::cerr << "Invalid probe log or output error\n";
    return 1;
  }
}
