#import <AppKit/AppKit.h>

#include <charconv>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "src/client_approvals.h"
#include "src/client_token.h"
#include "src/config.h"
#include "src/settings_document.h"
#include "src/user_paths.h"
#include "src/user_service.h"

namespace {
NSURL* SettingsURL() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  NSString* base = (xdg != nullptr && *xdg != '\0')
                       ? [NSString stringWithUTF8String:xdg]
                       : [NSHomeDirectory() stringByAppendingPathComponent:@".config"];
  return [NSURL fileURLWithPath:[[base stringByAppendingPathComponent:@"facepalm"]
                                    stringByAppendingPathComponent:@"facepalm.conf"]];
}

NSString* FormatFloat(float value) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return [[NSString alloc] initWithBytes:buffer
                                  length:result.ptr - buffer
                                encoding:NSUTF8StringEncoding];
}

std::string Utf8Bytes(NSString* text) {
  NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding];
  if (data.length == 0) return {};
  return {static_cast<const char*>(data.bytes), data.length};
}

NSString* ErrorMessage(const std::string& error) {
  NSString* message = [NSString stringWithUTF8String:error.c_str()];
  return message == nil ? @"The settings file is invalid." : message;
}
// Reads a snapshot without mistaking an unreadable file for a missing one.
BOOL ReadSettingsFile(NSURL* url, NSData** data, BOOL* missing, NSString** message) {
  NSError* error = nil;
  NSData* content = [NSData dataWithContentsOfURL:url options:0 error:&error];
  if (content == nil) {
    if ([error.domain isEqualToString:NSCocoaErrorDomain] &&
        error.code == NSFileReadNoSuchFileError) {
      *missing = YES;
      *data = [NSData data];
      return YES;
    }
    *message = error.localizedDescription != nil ? error.localizedDescription
                                                 : @"The settings file cannot be read.";
    return NO;
  }
  if (content.length > 1024 * 1024) {
    *message = @"The settings file is too large.";
    return NO;
  }
  *data = content;
  *missing = NO;
  return YES;
}

BOOL SaveSettingsFile(NSURL* url, NSData* expected, BOOL expectedMissing,
                      const std::string& updated, NSString** message) {
  NSData* current = nil;
  BOOL missing = NO;
  if (!ReadSettingsFile(url, &current, &missing, message)) return NO;
  if (missing != expectedMissing || ![current isEqualToData:expected]) {
    *message =
        @"The settings file changed while this window was open. Reopen Settings and try again.";
    return NO;
  }
  NSError* error = nil;
  if (![[NSFileManager defaultManager] createDirectoryAtURL:url.URLByDeletingLastPathComponent
                                withIntermediateDirectories:YES
                                                 attributes:@{
                                                   NSFilePosixPermissions : @0700
                                                 }
                                                      error:&error]) {
    *message = error.localizedDescription;
    return NO;
  }
  NSData* output = [NSData dataWithBytes:updated.data() length:updated.size()];
  if (![output writeToURL:url options:NSDataWritingAtomic error:&error]) {
    *message = error.localizedDescription;
    return NO;
  }
  return YES;
}
}  // namespace

namespace {
// Turns the user service on or off (#37): src/user_service.h holds the
// logic, following specs/UserService.tla; this only supplies the bundle's
// paths. Returns an error message, or nil.
NSString* SwitchService(BOOL on) {
  const std::string home = Utf8Bytes(NSHomeDirectory());
  const std::optional<facepalm::UserPaths> paths =
      facepalm::PathsFor(home, facepalm::Platform::kMacOs);
  if (!paths) return @"Your home folder could not be found.";
  const std::unique_ptr<facepalm::ApprovalsIo> approvals_io = facepalm::PosixApprovalsIo();
  facepalm::ClientApprovals approvals(
      paths->clients, [](const std::string& path) { return facepalm::LoadClientToken(path); },
      *approvals_io);
  const std::unique_ptr<facepalm::ServiceIo> io = facepalm::PosixServiceIo();
  facepalm::UserService service(
      facepalm::Platform::kMacOs, home,
      [&approvals](std::string_view name, std::string& error) {
        return approvals.Approve(name, error);
      },
      *io);
  std::string error;
  if (!on) return service.TurnOff(error) ? nil : ErrorMessage(error);
  NSBundle* bundle = [NSBundle mainBundle];
  NSString* daemon = [bundle pathForAuxiliaryExecutable:@"facepalm-daemon"];
  NSString* model = [bundle pathForResource:@"face_landmarker" ofType:@"task" inDirectory:@"model"];
  if (daemon == nil || model == nil) return @"This copy of Facepalm has no daemon or face model.";
  NSString* settings = SettingsURL().path;
  const facepalm::ServiceProgram program{
      .daemon = Utf8Bytes(daemon),
      .model = Utf8Bytes(model),
      .config = [[NSFileManager defaultManager] fileExistsAtPath:settings] ? Utf8Bytes(settings)
                                                                           : std::string{}};
  return service.TurnOn(program, error) ? nil : ErrorMessage(error);
}

BOOL ServiceIsOn() {
  const std::unique_ptr<facepalm::ServiceIo> io = facepalm::PosixServiceIo();
  facepalm::UserService service(
      facepalm::Platform::kMacOs, Utf8Bytes(NSHomeDirectory()),
      [](std::string_view, std::string&) { return std::optional<std::string>(); }, *io);
  return service.IsOn() ? YES : NO;
}
}  // namespace

@interface FacepalmApp : NSObject <NSApplicationDelegate, NSMenuDelegate>
@property(nonatomic) BOOL openSettingsOnLaunch;
@property(nonatomic, strong) NSStatusItem* statusItem;
@property(nonatomic, strong) NSMenuItem* power;
@property(nonatomic, strong) NSWindow* settingsWindow;
@property(nonatomic, strong) NSTextField* frameRate;
@property(nonatomic, strong) NSButton* gestures;
@property(nonatomic, strong) NSButton* expressions;
@property(nonatomic, strong) NSMutableArray<NSTextField*>* advanced;
@property(nonatomic, strong) NSData* loadedData;
@property(nonatomic) BOOL loadedMissing;
@end

@implementation FacepalmApp
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
  self.statusItem.button.title = @"🤦";
  [self.statusItem.button setAccessibilityLabel:@"Facepalm"];
  self.statusItem.button.toolTip = @"Facepalm";
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Facepalm"];
  NSMenuItem* status = [[NSMenuItem alloc] initWithTitle:@"Camera status unavailable"
                                                  action:nil
                                           keyEquivalent:@""];
  status.enabled = NO;
  [menu addItem:status];
  self.power = [[NSMenuItem alloc] initWithTitle:@"Turn On Facepalm…"
                                          action:@selector(switchPower:)
                                   keyEquivalent:@""];
  self.power.target = self;
  [menu addItem:self.power];
  [menu addItem:[NSMenuItem separatorItem]];
  NSMenuItem* settings = [[NSMenuItem alloc] initWithTitle:@"Settings…"
                                                    action:@selector(openSettings:)
                                             keyEquivalent:@","];
  settings.target = self;
  [menu addItem:settings];
  NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit Facepalm Menu"
                                                action:@selector(terminate:)
                                         keyEquivalent:@"q"];
  quit.target = NSApp;
  [menu addItem:quit];
  menu.delegate = self;
  self.statusItem.menu = menu;
  [self menuNeedsUpdate:menu];
  if (self.openSettingsOnLaunch) [self openSettings:nil];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)application
                    hasVisibleWindows:(BOOL)hasVisibleWindows {
  (void)application;
  (void)hasVisibleWindows;
  [self openSettings:nil];
  return NO;
}

- (void)showError:(NSString*)message {
  [self showError:message title:@"Settings could not be saved"];
}

- (void)showError:(NSString*)message title:(NSString*)title {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = title;
  alert.informativeText = message;
  [alert runModal];
}

// Shows whether Facepalm is on each time the menu opens.
- (void)menuNeedsUpdate:(NSMenu*)menu {
  (void)menu;
  self.power.title = ServiceIsOn() ? @"Turn Off Facepalm" : @"Turn On Facepalm…";
}

// Turning on is the person's consent, so it asks first; turning off does not.
- (void)switchPower:(id)sender {
  (void)sender;
  const BOOL on = ServiceIsOn();
  if (!on) {
    NSAlert* consent = [[NSAlert alloc] init];
    consent.messageText = @"Turn on Facepalm?";
    consent.informativeText =
        @"Facepalm will use your camera while an app you approved, such as Claude, asks how "
        @"you react, and it starts again each time you log in until you turn it off. Frames "
        @"stay on this Mac.";
    [consent addButtonWithTitle:@"Turn On"];
    [consent addButtonWithTitle:@"Cancel"];
    [NSApp activateIgnoringOtherApps:YES];
    if ([consent runModal] != NSAlertFirstButtonReturn) return;
  }
  NSString* error = SwitchService(!on);
  if (error != nil) {
    [self showError:error
              title:on ? @"Facepalm could not be turned off" : @"Facepalm could not be turned on"];
  }
}

- (void)openSettings:(id)sender {
  (void)sender;
  if (self.settingsWindow.visible) {
    [NSApp activateIgnoringOtherApps:YES];
    [self.settingsWindow makeKeyAndOrderFront:nil];
    return;
  }
  NSData* data = nil;
  BOOL missing = NO;
  NSString* message = nil;
  if (!ReadSettingsFile(SettingsURL(), &data, &missing, &message)) {
    [self showError:message];
    return;
  }
  NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  if (text == nil) {
    [self showError:@"The settings file is not UTF-8."];
    return;
  }
  std::string error;
  const std::string original = data.length == 0
                                   ? std::string{}
                                   : std::string(static_cast<const char*>(data.bytes), data.length);
  if (original.find('\0') != std::string::npos) {
    [self showError:@"The settings file contains a NUL character."];
    return;
  }
  const auto config = facepalm::ParseConfig(original, error);
  if (!config) {
    [self showError:ErrorMessage(error)];
    return;
  }
  self.loadedData = data;
  self.loadedMissing = missing;
  if (self.settingsWindow == nil) [self createSettingsWindow];
  self.frameRate.integerValue = config->frames_per_second;
  self.gestures.state = config->gestures_enabled ? NSControlStateValueOn : NSControlStateValueOff;
  self.expressions.state =
      config->expressions_enabled ? NSControlStateValueOn : NSControlStateValueOff;
  NSArray<NSString*>* tuning = @[
    FormatFloat(config->gesture_confidence_threshold),
    FormatFloat(config->expressions_smallest_rise), FormatFloat(config->expressions_spreads),
    [NSString stringWithFormat:@"%d", config->expressions_hold_ms],
    FormatFloat(config->expressions_pose_degrees), FormatFloat(config->expressions_pose_range),
    [NSString stringWithFormat:@"%d", config->expressions_settle_ms]
  ];
  for (NSUInteger i = 0; i < tuning.count; ++i) self.advanced[i].stringValue = tuning[i];
  [NSApp activateIgnoringOtherApps:YES];
  [self.settingsWindow center];
  [self.settingsWindow makeKeyAndOrderFront:nil];
}

- (void)createSettingsWindow {
  const NSRect bounds = NSMakeRect(0, 0, 560, 610);
  self.settingsWindow =
      [[NSWindow alloc] initWithContentRect:bounds
                                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  self.settingsWindow.title = @"Facepalm Settings";
  self.settingsWindow.releasedWhenClosed = NO;
  NSView* content = self.settingsWindow.contentView;
  NSTextField* note = [NSTextField
      wrappingLabelWithString:@"Facepalm reads these settings when it starts: turn it off and "
                              @"on again from the menu to apply them."];
  note.frame = NSMakeRect(24, 548, 512, 44);
  [content addSubview:note];
  NSTextField* fpsLabel = [NSTextField labelWithString:@"Frames per second (1–60)"];
  fpsLabel.frame = NSMakeRect(24, 510, 300, 24);
  [content addSubview:fpsLabel];
  self.frameRate = [[NSTextField alloc] initWithFrame:NSMakeRect(400, 510, 110, 24)];
  self.frameRate.accessibilityLabel = @"Frames per second";
  [content addSubview:self.frameRate];
  self.gestures = [NSButton checkboxWithTitle:@"Detect head gestures" target:nil action:nil];
  self.gestures.frame = NSMakeRect(24, 476, 350, 24);
  [content addSubview:self.gestures];
  self.expressions = [NSButton checkboxWithTitle:@"Detect facial expressions"
                                          target:nil
                                          action:nil];
  self.expressions.frame = NSMakeRect(24, 444, 350, 24);
  [content addSubview:self.expressions];
  NSTextField* tuningTitle = [NSTextField labelWithString:@"Advanced tuning (next start)"];
  tuningTitle.frame = NSMakeRect(24, 408, 350, 24);
  tuningTitle.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
  [content addSubview:tuningTitle];
  NSArray<NSString*>* labels = @[
    @"Gesture confidence (0–1)", @"Expression smallest rise (0–1)", @"Expression spreads (0–100)",
    @"Expression hold (0–5000 ms)", @"Pose degrees (0–180)", @"Pose range (0–180)",
    @"Pose settle (0–5000 ms)"
  ];
  self.advanced = [NSMutableArray array];
  for (NSUInteger i = 0; i < labels.count; ++i) {
    const CGFloat y = 376 - static_cast<CGFloat>(i) * 32;
    NSTextField* label = [NSTextField labelWithString:labels[i]];
    label.frame = NSMakeRect(24, y, 350, 24);
    [content addSubview:label];
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(400, y, 110, 24)];
    field.accessibilityLabel = labels[i];
    [self.advanced addObject:field];
    [content addSubview:field];
  }
  NSTextField* configPath = [NSTextField wrappingLabelWithString:SettingsURL().path];
  configPath.frame = NSMakeRect(24, 64, 512, 50);
  configPath.selectable = YES;
  [content addSubview:configPath];
  NSButton* save = [NSButton buttonWithTitle:@"Save Settings"
                                      target:self
                                      action:@selector(saveSettings:)];
  save.frame = NSMakeRect(400, 20, 136, 32);
  save.keyEquivalent = @"\r";
  [content addSubview:save];
}

- (void)saveSettings:(id)sender {
  (void)sender;
  NSString* number = self.frameRate.stringValue;
  NSScanner* scanner = [NSScanner scannerWithString:number];
  NSInteger fps = 0;
  if (![scanner scanInteger:&fps] || !scanner.isAtEnd || fps < 1 || fps > 60) {
    [self showError:@"Enter a whole number from 1 to 60 for frames per second."];
    return;
  }
  NSData* current = nil;
  BOOL missing = NO;
  NSString* message = nil;
  if (!ReadSettingsFile(SettingsURL(), &current, &missing, &message)) {
    [self showError:message];
    return;
  }
  if (missing != self.loadedMissing || ![current isEqualToData:self.loadedData]) {
    [self showError:@"The settings file changed while this window was open. Reopen Settings and "
                    @"try again."];
    return;
  }
  std::string error;
  facepalm::MenuSettings settings{
      .frames_per_second = static_cast<int>(fps),
      .gestures_enabled = self.gestures.state == NSControlStateValueOn,
      .expressions_enabled = self.expressions.state == NSControlStateValueOn,
      .gesture_confidence = Utf8Bytes(self.advanced[0].stringValue),
      .expression_rise = Utf8Bytes(self.advanced[1].stringValue),
      .expression_spreads = Utf8Bytes(self.advanced[2].stringValue),
      .expression_hold_ms = Utf8Bytes(self.advanced[3].stringValue),
      .expression_pose_degrees = Utf8Bytes(self.advanced[4].stringValue),
      .expression_pose_range = Utf8Bytes(self.advanced[5].stringValue),
      .expression_settle_ms = Utf8Bytes(self.advanced[6].stringValue),
  };
  const auto updated = facepalm::UpdateSettingsDocument(
      std::string_view(current.length == 0 ? "" : static_cast<const char*>(current.bytes),
                       current.length),
      std::move(settings), error);
  if (!updated) {
    [self showError:ErrorMessage(error)];
    return;
  }
  if (!SaveSettingsFile(SettingsURL(), self.loadedData, self.loadedMissing, *updated, &message)) {
    [self showError:message];
    return;
  }
  [self.settingsWindow close];
}
@end

#ifndef FACEPALM_MENU_NO_MAIN
int main(int argc, char** argv) {
  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    FacepalmApp* delegate = [[FacepalmApp alloc] init];
    for (int i = 1; i < argc; ++i) {
      if (std::string_view(argv[i]) == "--settings") delegate.openSettingsOnLaunch = YES;
    }
    app.delegate = delegate;
    [app run];
  }
  return 0;
}

#endif  // FACEPALM_MENU_NO_MAIN
