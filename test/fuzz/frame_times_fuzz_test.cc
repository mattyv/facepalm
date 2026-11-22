#include <string>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "src/frame_times.h"

namespace facepalm {
namespace {

// Fuzz target 4 (#120): a recording's manifest is camera_probe's own
// timestamps.txt, which a person could edit or a crash could truncate, so
// `FrameTimes` must never crash on whatever it holds, for whatever image
// names are asked of it. Two arguments vary together here, so the seed is
// a literal tuple (ReadFilesFromDirectory seeds one argument's domain from
// a corpus of files, which does not fit a pair like this one).
void DoesNotCrash(const std::string& manifest,
                  const std::vector<std::string>& images) {
  std::string error;
  FrameTimes(manifest, images, error);
}
FUZZ_TEST(FrameTimesFuzz, DoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>(),
                 fuzztest::Arbitrary<std::vector<std::string>>())
    .WithSeeds({{"1 1000\n2 1067\n3 1200\n",
                 {"rec/frame_000001.png", "rec/frame_000002.png",
                  "rec/frame_000003.png"}}});

}  // namespace
}  // namespace facepalm
