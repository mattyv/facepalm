#ifndef FACEPALM_SRC_POST_H_
#define FACEPALM_SRC_POST_H_

#include <functional>

namespace facepalm {

// Runs a task on a serial queue.
using Post = std::function<void(std::function<void()>)>;

}  // namespace facepalm

#endif  // FACEPALM_SRC_POST_H_
