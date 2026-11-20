#include <iostream>

#include "src/gesture_replay.h"

int main() {
  if (!facepalm::ReplayGestures(std::cin, std::cout)) {
    std::cerr << "Invalid gesture log or output error\n";
    return 1;
  }
}
