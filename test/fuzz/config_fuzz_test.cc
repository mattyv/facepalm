#include <string>

#include "fuzztest/fuzztest.h"
#include "src/config.h"

namespace facepalm {
namespace {

// Fuzz target 2 (#120): the config file is one a person or another program
// can edit by hand, so `ParseConfig` must never crash on whatever text it
// holds.
void ParseDoesNotCrash(const std::string& text) {
  std::string error;
  ParseConfig(text, error);
}
FUZZ_TEST(ConfigFuzz, ParseDoesNotCrash)
    .WithDomains(fuzztest::Arbitrary<std::string>())
    .WithSeeds(fuzztest::ReadFilesFromDirectory(FACEPALM_FUZZ_CORPUS));

}  // namespace
}  // namespace facepalm
