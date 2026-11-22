#import "tools/menu/calibration_window_macos.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "src/calibration.h"
#include "src/calibration_flow.h"
#include "tools/posix_calibrate_client_io.h"

namespace {

constexpr CGFloat kWindowWidth = 420;
constexpr CGFloat kWindowHeight = 180;
constexpr CGFloat kMargin = 24;
constexpr CGFloat kButtonWidth = 110;
constexpr CGFloat kButtonHeight = 32;

NSString* const kIntroText = @"Calibration is optional. It teaches Facepalm where you look on each "
                             @"of your screens, which makes the feelings and reactions it reports "
                             @"more accurate; everything else keeps working without it.";

// #137's fixate-and-turn step.
NSString* const kGainsStepText =
    @"Look at the dot the way you normally would and slowly turn your head "
    @"left, then right, keeping your eyes on it the whole time. Then click "
    @"Next.";

// Every screen AppKit reports, main first then the rest in [NSScreen
// screens]' own order -- the same order MacosScreenSource::Screens()
// (src/macos/screen_context_macos.mm) enumerates them in, which the
// daemon's own screen index (CalibrationFlow::View::screen) is counted
// against.
NSArray<NSScreen*>* OrderedScreens() {
  NSArray<NSScreen*>* screens = [NSScreen screens];
  NSScreen* main = [NSScreen mainScreen];
  NSMutableArray<NSScreen*>* ordered = [NSMutableArray arrayWithCapacity:screens.count];
  if (main != nil) {
    [ordered addObject:main];
  }
  for (NSScreen* screen in screens) {
    if (screen != main) {
      [ordered addObject:screen];
    }
  }
  return ordered;
}

// `screen`, or nil once `index` is past the last one AppKit reports (the
// window simply stays where it was, rather than this method's caller
// moving it onto nothing).
NSScreen* ScreenAt(int index) {
  NSArray<NSScreen*>* screens = OrderedScreens();
  if (index < 0 || static_cast<NSUInteger>(index) >= screens.count) {
    return nil;
  }
  return screens[static_cast<NSUInteger>(index)];
}

// Where `screen` sits relative to the main one: "the main screen", or its
// side -- left, right, above or below -- by whichever of the two, x or y,
// moved further from the main screen's own origin (#135: "the screen the
// prompt names").
NSString* ScreenPosition(NSScreen* screen) {
  NSScreen* main = [NSScreen mainScreen];
  if (screen == main || main == nil) {
    return @"the main screen";
  }
  const CGFloat dx = screen.frame.origin.x - main.frame.origin.x;
  const CGFloat dy = screen.frame.origin.y - main.frame.origin.y;
  if (std::abs(dx) >= std::abs(dy)) {
    return dx < 0 ? @"the screen to the left" : @"the screen to the right";
  }
  // AppKit's y grows upward, unlike most screen coordinate systems.
  return dy < 0 ? @"the screen below" : @"the screen above";
}

// "the screen to the left (Dell U2720Q)", naming both the screen's
// position and its display name (#135, #137), or just the position for a
// screen AppKit cannot name.
NSString* ScreenDescription(NSScreen* screen) {
  NSString* position = ScreenPosition(screen);
  if (screen.localizedName.length == 0) {
    return position;
  }
  return [NSString stringWithFormat:@"%@ (%@)", position, screen.localizedName];
}

// The step this one running CalibrationFlow::View prompts, following
// specs/Calibration.tla's own targets (the resting pose, #137's gains
// step, then each screen's centre and four corners), naming the screen a
// point's own prompt is for (#135).
NSString* StepPrompt(const facepalm::CalibrationFlow::View& view) {
  if (view.resting_step) {
    return @"Sit the way you normally would and look at the centre of "
           @"the screen in front of you, then click Next.";
  }
  if (view.gains_step) {
    return kGainsStepText;
  }
  NSString* where = @"the centre";
  switch (view.point) {
    case facepalm::CalibrationPoint::kCentre:
      where = @"the centre";
      break;
    case facepalm::CalibrationPoint::kTopLeft:
      where = @"the top-left corner";
      break;
    case facepalm::CalibrationPoint::kTopRight:
      where = @"the top-right corner";
      break;
    case facepalm::CalibrationPoint::kBottomLeft:
      where = @"the bottom-left corner";
      break;
    case facepalm::CalibrationPoint::kBottomRight:
      where = @"the bottom-right corner";
      break;
  }
  NSScreen* screen = ScreenAt(view.screen);
  NSString* screenName = screen != nil ? ScreenDescription(screen)
                                       : [NSString stringWithFormat:@"screen %d", view.screen + 1];
  return [NSString stringWithFormat:@"Look at %@ of %@, the way you normally would, then click Next.",
                                    where, screenName];
}

// "Calibrated 1 screen." or "Calibrated 3 screens." (#135, #137): the
// wizard's own result line, rather than ending silently (#136's finding).
NSString* ResultText(int screens) {
  return [NSString stringWithFormat:@"Calibrated %d screen%s.", screens, screens == 1 ? "" : "s"];
}

}  // namespace

@interface CalibrationWindowController ()
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSTextField* label;
@property(nonatomic, strong) NSButton* primaryButton;
@property(nonatomic, strong) NSButton* secondaryButton;
@property(nonatomic) BOOL started;
@end

@implementation CalibrationWindowController {
  facepalm::PosixCalibrateClientIo _io;
  std::unique_ptr<facepalm::CalibrationFlow> _flow;
}

- (instancetype)initWithSocketPath:(std::string)socketPath token:(std::string)token {
  self = [super init];
  if (self) {
    _flow =
        std::make_unique<facepalm::CalibrationFlow>(std::move(socketPath), std::move(token), _io);
  }
  return self;
}

- (void)show {
  [self createWindow];
  [self render];
  [NSApp activateIgnoringOtherApps:YES];
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
}

- (void)createWindow {
  self.window =
      [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)
                                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  self.window.title = @"Calibrate Facepalm";
  self.window.releasedWhenClosed = NO;
  self.window.delegate = self;
  NSView* content = self.window.contentView;
  self.label = [NSTextField wrappingLabelWithString:kIntroText];
  self.label.frame = NSMakeRect(kMargin, 64, kWindowWidth - 2 * kMargin, 92);
  [content addSubview:self.label];
  self.secondaryButton = [NSButton buttonWithTitle:@"Cancel"
                                            target:self
                                            action:@selector(secondaryClicked:)];
  self.secondaryButton.frame = NSMakeRect(kWindowWidth - 2 * kButtonWidth - 2 * kMargin, kMargin,
                                          kButtonWidth, kButtonHeight);
  [content addSubview:self.secondaryButton];
  self.primaryButton = [NSButton buttonWithTitle:@"Start Calibration"
                                          target:self
                                          action:@selector(primaryClicked:)];
  self.primaryButton.frame =
      NSMakeRect(kWindowWidth - kButtonWidth - kMargin, kMargin, kButtonWidth, kButtonHeight);
  self.primaryButton.keyEquivalent = @"\r";
  [content addSubview:self.primaryButton];
}

// Moves the window onto `view`'s own screen (#135): only a screen's point
// step names one (ScreenAt may also return nil past the last screen
// AppKit reports, in which case the window simply stays where it is).
- (void)moveToScreenForStep:(const facepalm::CalibrationFlow::View&)view {
  if (view.resting_step || view.gains_step) {
    return;
  }
  NSScreen* screen = ScreenAt(view.screen);
  if (screen == nil) {
    return;
  }
  const NSRect visible = screen.visibleFrame;
  const NSRect frame = self.window.frame;
  const NSPoint origin =
      NSMakePoint(visible.origin.x + (visible.size.width - frame.size.width) / 2,
                 visible.origin.y + (visible.size.height - frame.size.height) / 2);
  [self.window setFrameOrigin:origin];
}

// Shows CalibrationFlow::CurrentView(), before Start (the intro) and
// after each reply.
- (void)render {
  if (!self.started) {
    self.label.stringValue = kIntroText;
    self.primaryButton.title = @"Start Calibration";
    self.primaryButton.hidden = NO;
    self.secondaryButton.title = @"Cancel";
    return;
  }
  const facepalm::CalibrationFlow::View& view = self->_flow->CurrentView();
  switch (view.state) {
    case facepalm::CalibrationFlow::State::kPrompting:
      [self moveToScreenForStep:view];
      self.label.stringValue = StepPrompt(view);
      self.primaryButton.title = @"Next";
      self.primaryButton.hidden = NO;
      self.secondaryButton.title = @"Cancel";
      break;
    case facepalm::CalibrationFlow::State::kDone:
      self.label.stringValue =
          [NSString stringWithFormat:@"%@ The new profile is saved.", ResultText(view.screens)];
      self.primaryButton.hidden = YES;
      self.secondaryButton.title = @"Close";
      break;
    case facepalm::CalibrationFlow::State::kFailed:
      self.label.stringValue =
          [NSString stringWithFormat:@"Calibration could not finish: %s", view.error.c_str()];
      self.primaryButton.hidden = YES;
      self.secondaryButton.title = @"Close";
      break;
    case facepalm::CalibrationFlow::State::kClosed:
      // A cancel the daemon confirmed: nothing left to show.
      [self.window close];
      break;
  }
}

- (void)primaryClicked:(id)sender {
  (void)sender;
  if (!self.started) {
    self.started = YES;
    // The real screen count (#135), not a hardcoded one screen: every
    // screen [NSScreen screens] reports, the same count
    // CalibrationSession::kMaxScreens's own caller (the daemon) bounds.
    self->_flow->Start(static_cast<int>([NSScreen screens].count));
  } else if (self->_flow->CurrentView().state == facepalm::CalibrationFlow::State::kPrompting) {
    self->_flow->Next();
  }
  [self render];
}

- (void)secondaryClicked:(id)sender {
  (void)sender;
  if (self.started &&
      self->_flow->CurrentView().state == facepalm::CalibrationFlow::State::kPrompting) {
    self->_flow->Cancel();
    [self render];
    return;
  }
  [self.window close];
}

// CalibrationMenu.tla's WindowClosed: fires however the window closed
// (this method's own [self.window close] calls above included), and
// CalibrationFlow::WindowClosed is a no-op unless a step was showing, so
// calling it unconditionally here is always safe.
- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  self->_flow->WindowClosed();
  if (self.onClosed) {
    self.onClosed();
  }
}

@end
