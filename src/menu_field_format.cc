#include "src/menu_field_format.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace facepalm {
namespace {

constexpr std::size_t kBufferSize = 64;
constexpr std::string_view kBlank = " \t\r\n";

std::string_view Trim(std::string_view text) {
  const std::size_t first = text.find_first_not_of(kBlank);
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(kBlank) - first + 1);
}

}  // namespace

// NOLINTNEXTLINE(modernize-use-string-view): platform APIs consume owned text.
std::string CalibrationAction() { return "press Enter or click Next."; }

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Prompt, then hint.
std::string CalibrationPrompt(std::string_view prompt, std::string_view hint) {
  if (hint.empty()) {
    return std::string(prompt);
  }
  std::string reason(hint);
  reason[0] =
      static_cast<char>(std::toupper(static_cast<unsigned char>(reason[0])));
  return reason + ".\n\n" + std::string(prompt);
}

std::string FormatFloat(float value) {
  std::array<char, kBufferSize> buffer{};
  const auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return {buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())};
}

std::optional<int> ParseWholeNumberInRange(std::string_view text, int min,
                                           int max) {
  const std::string_view trimmed = Trim(text);
  int value = 0;
  const auto [end, error] =
      std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
  if (error != std::errc() || end != trimmed.data() + trimmed.size()) {
    return std::nullopt;
  }
  if (value < min || value > max) {
    return std::nullopt;
  }
  return value;
}

}  // namespace facepalm
