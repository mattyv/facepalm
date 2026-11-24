#define FACEPALM_MENU_NO_MAIN
#import "tools/menu/main.mm"

#include <string>

#include "gtest/gtest.h"

// AppKit-only round trips that a solitary C++ test cannot reach, since the
// decisions behind them moved to src/settings_save_guard.h and
// src/menu_field_format.h (#64), each covered there at 100 percent
// instead. Not a `*_test`: tools/ carries no coverage floor (the build
// only measures src/), so this stays a plain check, the way
// preview_jpeg_checks and avfoundation_session_checks already do.
namespace {

// UpdateSettingsDocument's own NUL rejection is covered directly in
// test/settings_document_test.cc; this checks only that Utf8Bytes itself
// does not lose or truncate an embedded NUL on the way out of a real
// NSString.
TEST(Utf8BytesCheck, EmbeddedNulSurvivesAppKitConversion) {
  const char value[] = {'0', '.', '5', '\0', 'x'};
  NSString* field = [[NSString alloc] initWithBytes:value
                                             length:sizeof(value)
                                           encoding:NSUTF8StringEncoding];
  EXPECT_EQ(Utf8Bytes(field).size(), sizeof(value));
}

// A fixture so TearDown removes the temporary directory even when an
// earlier ASSERT in the test body fails (#64): the bare TEST() this file
// had before, with its own cleanup line at the end, skipped that line on
// exactly that failure.
class MenuStoreCheck : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = [NSURL fileURLWithPath:[NSTemporaryDirectory()
                                            stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    target_ = [directory_ URLByAppendingPathComponent:@"settings.conf"];
  }
  void TearDown() override {
    [[NSFileManager defaultManager] removeItemAtURL:directory_ error:nil];
  }

  NSURL* directory_;
  NSURL* target_;
};

TEST_F(MenuStoreCheck, WritesAtomicallyAndRejectsAChangedFile) {
  NSData* original = nil;
  BOOL missing = NO;
  NSString* error = nil;
  ASSERT_TRUE(ReadSettingsFile(target_, &original, &missing, &error));
  ASSERT_TRUE(missing);
  ASSERT_TRUE(SaveSettingsFile(target_, original, missing, "# kept\n", &error));

  NSData* saved = [NSData dataWithContentsOfURL:target_];
  EXPECT_EQ(std::string(static_cast<const char*>(saved.bytes), saved.length), "# kept\n");

  EXPECT_FALSE(SaveSettingsFile(target_, original, missing, "overwrite\n", &error));
  NSData* after = [NSData dataWithContentsOfURL:target_];
  EXPECT_TRUE([saved isEqualToData:after]);
}

}  // namespace
