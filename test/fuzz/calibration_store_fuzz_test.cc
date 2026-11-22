#include <string>

#include "fuzztest/fuzztest.h"
#include "src/calibration_store.h"

namespace facepalm {
namespace {

// Fuzz target 5 (#120): the calibration profile is a file the daemon owns
// but a person could still replace or truncate, so
// `ParseCalibrationProfile` must never crash on whatever it holds.
void ParseDoesNotCrash(const std::string& text) {
  std::string error;
  ParseCalibrationProfile(text, error);
}
FUZZ_TEST(CalibrationStoreFuzz, ParseDoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>())
    .WithSeeds(fuzztest::ReadFilesFromDirectory(FACEPALM_FUZZ_CORPUS));

}  // namespace
}  // namespace facepalm
