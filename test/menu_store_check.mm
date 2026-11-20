#define FACEPALM_MENU_NO_MAIN
#import "tools/menu/main.mm"

#include <string>

#include "gtest/gtest.h"

namespace {
TEST(MenuStoreTest, EmbeddedNulSurvivesAppKitConversionAndFailsValidation) {
  const char value[] = {'0', '.', '5', '\0', 'x'};
  NSString* field = [[NSString alloc] initWithBytes:value
                                             length:sizeof(value)
                                           encoding:NSUTF8StringEncoding];
  facepalm::MenuSettings settings;
  settings.gesture_confidence = Utf8Bytes(field);
  std::string error;
  EXPECT_FALSE(facepalm::UpdateSettingsDocument("", std::move(settings), error));
}

TEST(MenuStoreTest, WritesAtomicallyAndRejectsChangedFile) {
  @autoreleasepool {
    NSURL* directory =
        [NSURL fileURLWithPath:[NSTemporaryDirectory()
                                   stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    NSURL* target = [directory URLByAppendingPathComponent:@"settings.conf"];
    NSData* original = nil;
    BOOL missing = NO;
    NSString* error = nil;
    ASSERT_TRUE(ReadSettingsFile(target, &original, &missing, &error));
    ASSERT_TRUE(missing);
    ASSERT_TRUE(SaveSettingsFile(target, original, missing, "# kept\n", &error));
    NSData* saved = [NSData dataWithContentsOfURL:target];
    EXPECT_EQ(std::string(static_cast<const char*>(saved.bytes), saved.length), "# kept\n");
    EXPECT_FALSE(SaveSettingsFile(target, original, missing, "overwrite\n", &error));
    NSData* after = [NSData dataWithContentsOfURL:target];
    EXPECT_TRUE([saved isEqualToData:after]);
    [[NSFileManager defaultManager] removeItemAtURL:directory error:nil];
  }
}
}  // namespace
