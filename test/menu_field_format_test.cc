#include "src/menu_field_format.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace facepalm {
namespace {

using ::testing::Eq;

TEST(CalibrationActionTest, NamesKeyboardAndMouseActions) {
  EXPECT_THAT(CalibrationAction(), Eq("press Enter or click Next."));
}

TEST(CalibrationPromptTest, KeepsTheTargetAndReasonOnRetry) {
  EXPECT_THAT(
      CalibrationPrompt(
          "Look at the top-left corner of the left screen, then click Next.",
          "the corner reading was too close to the centre reading"),
      Eq("The corner reading was too close to the centre reading.\n\n"
         "Look at the top-left corner of the left screen, then click Next."));
}

TEST(CalibrationPromptTest, LeavesThePromptAloneWithoutARefusal) {
  EXPECT_THAT(CalibrationPrompt("Look at the centre.", ""),
              Eq("Look at the centre."));
}

TEST(FormatFloatTest, FormatsAWholeNumberWithNoDecimalPoint) {
  EXPECT_THAT(FormatFloat(3.0F), Eq("3"));
}

TEST(FormatFloatTest, FormatsAFraction) {
  EXPECT_THAT(FormatFloat(0.5F), Eq("0.5"));
}

TEST(FormatFloatTest, FormatsANegativeNumber) {
  EXPECT_THAT(FormatFloat(-7.5F), Eq("-7.5"));
}

TEST(ParseWholeNumberInRangeTest, ParsesAnInRangeNumber) {
  EXPECT_THAT(ParseWholeNumberInRange("15", 1, 60), Eq(15));
}

TEST(ParseWholeNumberInRangeTest, TrimsSurroundingWhitespace) {
  EXPECT_THAT(ParseWholeNumberInRange("  15\t\n", 1, 60), Eq(15));
}

TEST(ParseWholeNumberInRangeTest, RefusesANumberBelowTheMinimum) {
  EXPECT_THAT(ParseWholeNumberInRange("0", 1, 60), Eq(std::nullopt));
}

TEST(ParseWholeNumberInRangeTest, RefusesANumberAboveTheMaximum) {
  EXPECT_THAT(ParseWholeNumberInRange("61", 1, 60), Eq(std::nullopt));
}

TEST(ParseWholeNumberInRangeTest, AcceptsTheMinimumAndMaximumThemselves) {
  EXPECT_THAT(ParseWholeNumberInRange("1", 1, 60), Eq(1));
  EXPECT_THAT(ParseWholeNumberInRange("60", 1, 60), Eq(60));
}

TEST(ParseWholeNumberInRangeTest, RefusesTrailingGarbage) {
  EXPECT_THAT(ParseWholeNumberInRange("15x", 1, 60), Eq(std::nullopt));
}

TEST(ParseWholeNumberInRangeTest, RefusesANonNumericField) {
  EXPECT_THAT(ParseWholeNumberInRange("fifteen", 1, 60), Eq(std::nullopt));
}

TEST(ParseWholeNumberInRangeTest, RefusesAnEmptyField) {
  EXPECT_THAT(ParseWholeNumberInRange("", 1, 60), Eq(std::nullopt));
}

TEST(ParseWholeNumberInRangeTest, RefusesAFraction) {
  EXPECT_THAT(ParseWholeNumberInRange("15.5", 1, 60), Eq(std::nullopt));
}

}  // namespace
}  // namespace facepalm
