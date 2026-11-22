#include "src/preview_readings.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/feelings.h"

// Each test stands for the issue #134 behavior it covers: the readings as
// they happen, each held briefly, and the latest beat's feeling.

namespace facepalm {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;

using PreviewReadingsTest = ::testing::Test;

TEST_F(PreviewReadingsTest, NothingYetIsEmpty) {
  const PreviewReadings readings;

  EXPECT_THAT(readings.Text(0), IsEmpty());
}

TEST_F(PreviewReadingsTest, AReadingShowsUntilItsHoldExpires) {
  PreviewReadings readings;
  readings.OnReading("Gesture: nod", 1000);

  EXPECT_THAT(readings.Text(1000), Eq("Gesture: nod"));
  EXPECT_THAT(readings.Text(1000 + PreviewReadings::kHoldMs),
              Eq("Gesture: nod"));
  EXPECT_THAT(readings.Text(1000 + PreviewReadings::kHoldMs + 1), IsEmpty());
}

TEST_F(PreviewReadingsTest, ANewReadingReplacesAnOlderOneAndResetsItsHold) {
  PreviewReadings readings;
  readings.OnReading("Gesture: nod", 0);
  readings.OnReading("Gesture: shake", PreviewReadings::kHoldMs);

  EXPECT_THAT(readings.Text(PreviewReadings::kHoldMs), Eq("Gesture: shake"));
  EXPECT_THAT(readings.Text(2 * PreviewReadings::kHoldMs),
              Eq("Gesture: shake"));
}

// FeedClients::Deliver's own "nothing recognized" case (an empty summary)
// changes nothing: the reading already showing, if any, keeps its hold.
TEST_F(PreviewReadingsTest, AnEmptySummaryIsIgnored) {
  PreviewReadings readings;
  readings.OnReading("Gesture: nod", 0);
  readings.OnReading("", 1);

  EXPECT_THAT(readings.Text(1), Eq("Gesture: nod"));
}

TEST_F(PreviewReadingsTest, ABeatsFeelingNeverExpiresOnItsOwn) {
  PreviewReadings readings;
  readings.OnBeat(Feeling::kConfused, BeatKind::kQuestion);

  EXPECT_THAT(readings.Text(0), Eq("Last read: confused (question)"));
  EXPECT_THAT(readings.Text(1'000'000), Eq("Last read: confused (question)"));
}

TEST_F(PreviewReadingsTest, ALaterBeatReplacesAnEarlierOne) {
  PreviewReadings readings;
  readings.OnBeat(Feeling::kConfused, BeatKind::kQuestion);
  readings.OnBeat(Feeling::kAgreement, BeatKind::kProposal);

  EXPECT_THAT(readings.Text(0), Eq("Last read: agreement (proposal)"));
}

TEST_F(PreviewReadingsTest, AReadingAndABeatShowTogetherOnTheirOwnLines) {
  PreviewReadings readings;
  readings.OnReading("Gesture: nod", 0);
  readings.OnBeat(Feeling::kAgreement, BeatKind::kQuestion);

  EXPECT_THAT(readings.Text(0),
              Eq("Gesture: nod\nLast read: agreement (question)"));
}

TEST_F(PreviewReadingsTest, ClearRemovesBothUntilSomethingNewHappens) {
  PreviewReadings readings;
  readings.OnReading("Gesture: nod", 0);
  readings.OnBeat(Feeling::kAgreement, BeatKind::kQuestion);
  readings.Clear();

  EXPECT_THAT(readings.Text(0), IsEmpty());
}

}  // namespace
}  // namespace facepalm
