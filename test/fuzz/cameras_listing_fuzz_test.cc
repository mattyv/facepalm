#include <string>

#include "fuzztest/fuzztest.h"
#include "src/cameras_listing.h"

namespace facepalm {
namespace {

// Fuzz target 6 (#120): the cameras listing is facepalm-daemon's own
// stdout, read by the menu apps' Settings window, so `ParseCamerasListing`
// must never crash on whatever bytes a daemon binary of any version wrote.
void DoesNotCrash(const std::string& text) { ParseCamerasListing(text); }
FUZZ_TEST(CamerasListingFuzz, DoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>())
    .WithSeeds(fuzztest::ReadFilesFromDirectory(FACEPALM_FUZZ_CORPUS));

}  // namespace
}  // namespace facepalm
