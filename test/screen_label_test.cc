#include "src/screen_label.h"

#include "gtest/gtest.h"

namespace facepalm {
namespace {

TEST(ScreenLabelTest, UsesModelAndManufacturerWithPosition) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "Dell",
                         .model = "U2723QE",
                         .connector = "DP-1",
                         .position = "the screen to the left"}),
            "the screen to the left (Dell U2723QE)");
}

TEST(ScreenLabelTest, DoesNotRepeatManufacturerAlreadyInModel) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "Dell",
                         .model = "Dell U2723QE",
                         .connector = "DP-1",
                         .position = "the main screen"}),
            "the main screen (Dell U2723QE)");
}

TEST(ScreenLabelTest, UsesModelWithoutManufacturer) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "U2723QE",
                         .connector = "DP-1",
                         .position = "the screen above"}),
            "the screen above (U2723QE)");
}

TEST(ScreenLabelTest, IdentifiesBuiltInFallbackByConnector) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "",
                         .connector = "eDP-1",
                         .position = "the main screen"}),
            "the main screen (built-in display)");
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "",
                         .connector = "LVDS-1",
                         .position = "the main screen"}),
            "the main screen (built-in display)");
}

TEST(ScreenLabelTest, BuiltInConnectorsTakePrecedenceOverReportedModel) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "Example",
                         .model = "PANEL123",
                         .connector = "eDP-1",
                         .position = "the main screen"}),
            "the main screen (built-in display)");
  EXPECT_EQ(ScreenLabel({.manufacturer = "Example",
                         .model = "PANEL456",
                         .connector = "LVDS-1",
                         .position = "the screen to the left"}),
            "the screen to the left (built-in display)");
}

TEST(ScreenLabelTest, UsesGenericFallbackForUnknownConnector) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "Dell",
                         .model = "",
                         .connector = "DP-1",
                         .position = "the screen below"}),
            "the screen below (external display)");
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "",
                         .connector = "HDMI-1",
                         .position = "the main screen"}),
            "the main screen (external display)");
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "",
                         .connector = "VGA-1",
                         .position = "the main screen"}),
            "the main screen (external display)");
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "",
                         .connector = "DVI-1",
                         .position = "the main screen"}),
            "the main screen (external display)");
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "",
                         .connector = "",
                         .position = "the main screen"}),
            "the main screen (display)");
}

TEST(ScreenLabelTest, TrimsPaddedMetadata) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "  Dell ",
                         .model = " Dell U2723QE ",
                         .connector = "DP-1",
                         .position = "the main screen"}),
            "the main screen (Dell U2723QE)");
}

TEST(ScreenLabelTest, TreatsBlankModelAsUnavailable) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "  ",
                         .model = " \t\n ",
                         .connector = "eDP-1",
                         .position = "the main screen"}),
            "the main screen (built-in display)");
}

TEST(ScreenLabelTest, DoesNotExposeUnknownConnector) {
  EXPECT_EQ(ScreenLabel({.manufacturer = "",
                         .model = "",
                         .connector = "Virtual-7",
                         .position = "the main screen"}),
            "the main screen (display)");
}

}  // namespace
}  // namespace facepalm
