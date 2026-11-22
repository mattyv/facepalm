#include "src/preview_readings.h"

#include <cstdint>
#include <string>
#include <utility>

#include "src/feelings.h"

namespace facepalm {

void PreviewReadings::OnReading(std::string summary, std::int64_t at_ms) {
  if (summary.empty()) {
    return;
  }
  const std::scoped_lock lock(mutex_);
  reading_ = std::move(summary);
  reading_at_ms_ = at_ms;
}

void PreviewReadings::OnBeat(Feeling feeling, BeatKind kind) {
  const std::scoped_lock lock(mutex_);
  feeling_line_ = std::string("Last read: ") + std::string(Name(feeling)) +
                  " (" + std::string(Name(kind)) + ")";
}

void PreviewReadings::Clear() {
  const std::scoped_lock lock(mutex_);
  reading_.clear();
  reading_at_ms_ = 0;
  feeling_line_.clear();
}

std::string PreviewReadings::Text(std::int64_t now_ms) const {
  const std::scoped_lock lock(mutex_);
  std::string text;
  if (!reading_.empty() && now_ms - reading_at_ms_ <= kHoldMs) {
    text = reading_;
  }
  if (!feeling_line_.empty()) {
    if (!text.empty()) {
      text += '\n';
    }
    text += feeling_line_;
  }
  return text;
}

}  // namespace facepalm
