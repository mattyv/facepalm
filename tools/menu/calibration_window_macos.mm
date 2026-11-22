#import "tools/menu/calibration_window_macos.h"

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

NSString* const kIntroText = @"Calibration is optional. It teaches Facepalm where you look on this "
                             @"screen, which makes the feelings and reactions it reports more "
                             @"accurate; everything else keeps working without it.";

// The step this one running CalibrationFlow::View prompts, following
// specs/Calibration.tla's own targets (the resting pose, then each
// screen's centre and four corners).
NSString* StepPrompt(const facepalm::CalibrationFlow::View& view) {
  if (view.resting_step) {
    return @"Sit the way you normally would and look at the centre of "
           @"this screen, then click Next.";
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
  return [NSString stringWithFormat:@"Look at %@ of this screen, then click Next.", where];
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
      self.label.stringValue = StepPrompt(view);
      self.primaryButton.title = @"Next";
      self.primaryButton.hidden = NO;
      self.secondaryButton.title = @"Cancel";
      break;
    case facepalm::CalibrationFlow::State::kDone:
      self.label.stringValue = @"Calibration finished. The new profile is saved.";
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
    self->_flow->Start(1);
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
