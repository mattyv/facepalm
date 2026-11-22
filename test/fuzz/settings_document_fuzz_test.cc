#include <array>
#include <optional>
#include <string>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/config.h"
#include "src/settings_document.h"

namespace facepalm {
namespace {

// Fuzz target 2 (#120): the settings document is a person-editable file,
// so `UpdateSettingsDocument` must never crash on whatever it holds, with
// the menu's own default settings.
void UpdateDoesNotCrash(const std::string& original) {
  const MenuSettings settings;
  std::string error;
  UpdateSettingsDocument(original, settings, error);
}
FUZZ_TEST(SettingsDocumentFuzz, UpdateDoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>())
    .WithSeeds(fuzztest::ReadFilesFromDirectory(FACEPALM_FUZZ_CORPUS));

// Every known setting, at a value `ParseConfig` and `UpdateSettingsDocument`
// both already accept: the same text test/fuzz/corpus/config/all_settings.conf
// holds, so the original document this test builds always parses.
constexpr std::array<std::string_view, 18> kBaselineLines = {
    "camera.frames_per_second = 24",
    "camera.device_id =",
    "head_pose.enabled = true",
    "gestures.enabled = true",
    "expressions.enabled = true",
    "gestures.confidence_threshold = 0.5",
    "expressions.smallest_rise = 0.06",
    "expressions.spreads = 3",
    "expressions.hold_ms = 200",
    "expressions.pose_degrees = 10",
    "expressions.pose_range = 3",
    "expressions.settle_ms = 600",
    "looking_away.enabled = true",
    "looking_away.degrees = 15",
    "looking_away.hold_ms = 1000",
    "screen_context.enabled = true",
    "screen_context.window_titles_enabled = false",
    "screen_context.other_screen_degrees = 7.5"};

// A valid settings document of every known setting at a value already
// known to parse, with `markers` as their own "# <marker>" comment lines
// spread through it, each distinct from every known setting name.
std::string OriginalWithMarkers(const std::vector<std::string>& markers) {
  std::string text;
  std::size_t next_marker = 0;
  for (const std::string_view line : kBaselineLines) {
    if (next_marker < markers.size()) {
      text += "# " + markers[next_marker++] + "\n";
    }
    text += std::string(line) + "\n";
  }
  while (next_marker < markers.size()) {
    text += "# " + markers[next_marker++] + "\n";
  }
  return text;
}

// Property test (#120): setting a key keeps every unrelated line and
// comment. `MenuSettings{}`'s defaults are themselves valid, so this always
// succeeds; every marker comment this test wove through the original must
// come back out of the result unchanged.
void PreservesUnrelatedLines(const std::vector<std::string>& markers) {
  const std::string original = OriginalWithMarkers(markers);
  const MenuSettings settings;
  std::string error;
  const std::optional<std::string> updated =
      UpdateSettingsDocument(original, settings, error);
  ASSERT_TRUE(updated.has_value()) << error;
  for (const std::string& marker : markers) {
    EXPECT_NE(updated->find("# " + marker), std::string::npos)
        << "lost comment: " << marker;
  }
}
FUZZ_TEST(SettingsDocumentFuzz, PreservesUnrelatedLines)
    .WithDomains(fuzztest::VectorOf(fuzztest::StringOf(fuzztest::InRange(' ',
                                                                         '~')))
                     .WithMaxSize(5));

}  // namespace
}  // namespace facepalm
