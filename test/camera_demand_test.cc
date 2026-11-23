#include "src/camera_demand.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Each test stands for the specs/PreviewLease.tla action or property it is
// named for.

namespace facepalm {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

class CameraDemandTest : public ::testing::Test {
 protected:
  std::vector<bool> changes_;
  CameraDemand demand_{[this](bool wanted) { changes_.push_back(wanted); }};
};

TEST_F(CameraDemandTest, StartsWithNothingWanted) {
  EXPECT_FALSE(demand_.Wanted());
  EXPECT_THAT(changes_, IsEmpty());
}

// specs/PreviewLease.tla's CameraOpens: the preview's own demand alone
// opens the camera (#176), with no approved client ever subscribed.
TEST_F(CameraDemandTest, PreviewDemandAloneIsWanted) {
  demand_.SetPreviewDemand(true);

  EXPECT_TRUE(demand_.Wanted());
  EXPECT_THAT(changes_, ElementsAre(true));
}

// The pre-#176 path still works: an approved client alone is wanted.
TEST_F(CameraDemandTest, ClientDemandAloneIsWanted) {
  demand_.SetClientDemand(true);

  EXPECT_TRUE(demand_.Wanted());
  EXPECT_THAT(changes_, ElementsAre(true));
}

// A second source joining while the first already holds it changes
// nothing (specs/PreviewLease.tla's CameraOpens fires only on Demand's
// own edge, not on every source's own change).
TEST_F(CameraDemandTest, ASecondSourceJoiningChangesNothing) {
  demand_.SetClientDemand(true);
  changes_.clear();

  demand_.SetPreviewDemand(true);

  EXPECT_TRUE(demand_.Wanted());
  EXPECT_THAT(changes_, IsEmpty());
}

// specs/PreviewLease.tla's CameraCloses waits for every source
// (AbandonedPreviewReleasesCamera): one source leaving while the other
// still wants it changes nothing.
TEST_F(CameraDemandTest, OneSourceLeavingWhileTheOtherStillWantsItStaysOn) {
  demand_.SetClientDemand(true);
  demand_.SetPreviewDemand(true);
  changes_.clear();

  demand_.SetPreviewDemand(false);

  EXPECT_TRUE(demand_.Wanted());
  EXPECT_THAT(changes_, IsEmpty());
}

// CameraCloses: the last source leaving releases it.
TEST_F(CameraDemandTest, TheLastSourceLeavingReleasesIt) {
  demand_.SetClientDemand(true);
  demand_.SetPreviewDemand(true);
  demand_.SetPreviewDemand(false);
  changes_.clear();

  demand_.SetClientDemand(false);

  EXPECT_FALSE(demand_.Wanted());
  EXPECT_THAT(changes_, ElementsAre(false));
}

// Asking again for a source already counted changes nothing.
TEST_F(CameraDemandTest, AskingAgainForTheSameSourceChangesNothing) {
  demand_.SetPreviewDemand(true);
  changes_.clear();

  demand_.SetPreviewDemand(true);

  EXPECT_THAT(changes_, IsEmpty());
}

// Releasing a source that was never counted changes nothing.
TEST_F(CameraDemandTest, ReleasingWithNothingWantedChangesNothing) {
  demand_.SetClientDemand(false);
  EXPECT_THAT(changes_, IsEmpty());
}

}  // namespace
}  // namespace facepalm
