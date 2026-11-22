#include "src/preview_publisher.h"

#include <utility>

namespace facepalm {

PreviewPublisher::PreviewPublisher(
    std::function<bool()> requested, PreviewEncoder encode,
    std::function<bool(const std::string&)> publish, PreviewLease& lease)
    : requested_(std::move(requested)),
      encode_(std::move(encode)),
      publish_(std::move(publish)),
      lease_(lease) {}

void PreviewPublisher::OnFrame(const Frame& frame) {
  const bool requested = requested_();
  if (requested) {
    if (!lease_.IsStreaming()) {
      lease_.Start();
    }
  } else if (lease_.IsStreaming()) {
    lease_.Stop();
  }
  if (!lease_.IsStreaming()) {
    return;
  }
  const std::optional<std::string> encoded = encode_(frame);
  if (!encoded) {
    return;
  }
  publish_(*encoded);
}

}  // namespace facepalm
