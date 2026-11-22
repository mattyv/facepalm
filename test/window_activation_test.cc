#include "src/window_activation.h"

#include <gtest/gtest.h>

// Each test stands for one of #146's rules: Command-Tab must reach
// Settings, the preview or the calibration wizard while one is open, so
// WindowActivationTracker's count decides when tools/menu/main.mm flips
// NSApp's activation policy.

namespace facepalm {
namespace {

TEST(WindowActivationTrackerTest, TheFirstWindowToOpenAsksToBecomeRegular) {
  WindowActivationTracker tracker;
  EXPECT_EQ(tracker.WindowOpened(), ActivationPolicyChange::kBecomeRegular);
  EXPECT_EQ(tracker.OpenCount(), 1);
}

TEST(WindowActivationTrackerTest, ASecondWindowOpeningAsksForNothing) {
  WindowActivationTracker tracker;
  tracker.WindowOpened();
  EXPECT_EQ(tracker.WindowOpened(), ActivationPolicyChange::kNone);
  EXPECT_EQ(tracker.OpenCount(), 2);
}

TEST(WindowActivationTrackerTest, ClosingOneOfTwoAsksForNothing) {
  WindowActivationTracker tracker;
  tracker.WindowOpened();
  tracker.WindowOpened();
  EXPECT_EQ(tracker.WindowClosed(), ActivationPolicyChange::kNone);
  EXPECT_EQ(tracker.OpenCount(), 1);
}

TEST(WindowActivationTrackerTest, ClosingTheLastOneAsksToBecomeAccessory) {
  WindowActivationTracker tracker;
  tracker.WindowOpened();
  EXPECT_EQ(tracker.WindowClosed(), ActivationPolicyChange::kBecomeAccessory);
  EXPECT_EQ(tracker.OpenCount(), 0);
}

// A close with nothing open -- which a correct caller never sends, since
// every close follows a matching open -- is a no-op rather than an
// underflow, so one extra, unmatched close notification never flips the
// policy back a second time.
TEST(WindowActivationTrackerTest, ClosingWithNothingOpenAsksForNothing) {
  WindowActivationTracker tracker;
  EXPECT_EQ(tracker.WindowClosed(), ActivationPolicyChange::kNone);
  EXPECT_EQ(tracker.OpenCount(), 0);
}

TEST(WindowActivationTrackerTest, OpeningAgainAfterTheLastOneClosedAsksAgain) {
  WindowActivationTracker tracker;
  tracker.WindowOpened();
  tracker.WindowClosed();
  EXPECT_EQ(tracker.WindowOpened(), ActivationPolicyChange::kBecomeRegular);
  EXPECT_EQ(tracker.OpenCount(), 1);
}

}  // namespace
}  // namespace facepalm
