#include <algorithm>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/session_camera.h"

namespace facepalm {
namespace {

using FrameRateRange = CaptureSession::FrameRateRange;
using FrameRates = CaptureSession::FrameRates;

fuzztest::Domain<FrameRateRange> AnyRange() {
  return fuzztest::Map(
      [](int min, int extra) {
        return FrameRateRange{.min = min, .max = min + extra};
      },
      fuzztest::InRange(1, 60), fuzztest::InRange(0, 60));
}

fuzztest::Domain<FrameRates> AnyRates() {
  return fuzztest::VectorOf(AnyRange()).WithMaxSize(5);
}

// Property test (#43, #120): the chosen rate always lies within one of the
// camera's own supported ranges, or is the one asked for when the camera
// reports none.
void ChosenRateWithinSupportedRange(int wanted, const FrameRates& rates) {
  const int chosen = NearestSupportedRate(wanted, rates);
  if (rates.empty()) {
    EXPECT_EQ(chosen, wanted);
    return;
  }
  EXPECT_TRUE(std::ranges::any_of(rates, [&](const FrameRateRange& range) {
    return chosen >= range.min && chosen <= range.max;
  }));
}
FUZZ_TEST(SessionCameraFuzz, ChosenRateWithinSupportedRange)
    .WithDomains(fuzztest::Arbitrary<int>(), AnyRates());

}  // namespace
}  // namespace facepalm
