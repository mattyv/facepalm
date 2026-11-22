#import <AppKit/AppKit.h>

#include <string>

// A small AppKit window around src/calibration_flow.h's CalibrationFlow
// (#118, #36, #35, item 12 of #61): kept apart from FacepalmApp
// (tools/menu/main.mm) so the wizard's own window, label and buttons add
// nothing to that class's complexity, the way tools/menu/main_linux.cc's
// CameraPreview is kept apart from main() (#64). Every decision about
// what to send and show is CalibrationFlow's; this only renders its
// CurrentView() and forwards button clicks and the window's close.
@interface CalibrationWindowController : NSObject <NSWindowDelegate>

// `socketPath` and `token` are resolved by the caller (src/user_paths.h,
// the "calibrate" client's own token, approved the same way SwitchService
// approves its own client in main.mm).
- (instancetype)initWithSocketPath:(std::string)socketPath token:(std::string)token;

// Called once the window this controller owns has closed, however it
// closed (Cancel, Close once done or failed, or the window's own close
// button) -- CalibrationMenu.tla's WindowClosed never leaves a session
// running unattended before this fires. The owner's only job is to drop
// its own strong reference, the way FacepalmApp's `calibrationController`
// does; this controller does not hold one to itself.
@property(nonatomic, copy) void (^onClosed)(void);

// Shows the window: a plain explanation that calibration is optional,
// with a button to begin it.
- (void)show;

@end
