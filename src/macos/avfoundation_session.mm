#include "src/macos/avfoundation_session.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

// Hands each sample buffer to a C++ callback, as MediaPipe's
// MPPCameraInputSource is its own sample buffer delegate.
namespace {

// A capture time in milliseconds, on the capture session's clock.
std::int64_t MillisecondsOf(CMTime time) {
  return CMTimeConvertScale(time, 1000, kCMTimeRoundingMethod_RoundTowardNegativeInfinity).value;
}

}  // namespace

@interface FacepalmFrameDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (instancetype)initWithHandler:(std::function<void(const facepalm::Frame&)>)handler;
// The open attempt each frame from here on is stamped with (#40).
- (void)setAttempt:(std::uint64_t)attempt;
@end

@implementation FacepalmFrameDelegate {
  std::function<void(const facepalm::Frame&)> _handler;
  std::atomic<std::uint64_t> _attempt;
}

- (instancetype)initWithHandler:(std::function<void(const facepalm::Frame&)>)handler {
  if ((self = [super init])) {
    _handler = std::move(handler);
  }
  return self;
}

- (void)setAttempt:(std::uint64_t)attempt {
  _attempt.store(attempt, std::memory_order_release);
}

- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
  CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (pixels == nullptr) {
    return;
  }
  CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
  const facepalm::Frame frame{
      .width = static_cast<int>(CVPixelBufferGetWidth(pixels)),
      .height = static_cast<int>(CVPixelBufferGetHeight(pixels)),
      .bytes_per_row = CVPixelBufferGetBytesPerRow(pixels),
      .bgra = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(pixels)),
      .timestamp_ms = MillisecondsOf(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)),
      // Stamped as the callback starts, so a delay after it cannot move the
      // frame into a later attempt.
      .attempt = _attempt.load(std::memory_order_acquire),
  };
  _handler(frame);
  CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
}

@end

namespace facepalm {
namespace {

// The unique ID of the camera a device notification is about.
std::string UniqueId(NSNotification* note) {
  AVCaptureDevice* device = note.object;
  return device.uniqueID == nil ? std::string() : std::string(device.uniqueID.UTF8String);
}

}  // namespace

struct AvFoundationSession::Impl {
  AVCaptureSession* session = [[AVCaptureSession alloc] init];
  dispatch_queue_t capture_queue =
      dispatch_queue_create("facepalm.camera.capture", DISPATCH_QUEUE_SERIAL);
  FacepalmFrameDelegate* delegate = nil;
  AVCaptureDevice* device = nil;
  NSArray<id<NSObject>>* observers = @[];
};

AvFoundationSession::AvFoundationSession(std::function<void(const Frame&)> on_frame,
                                         std::function<void()> on_session_failed,
                                         std::function<void(const std::string&)> on_disconnected,
                                         std::function<void(const std::string&)> on_connected)
    : impl_(std::make_unique<Impl>()) {
  impl_->delegate = [[FacepalmFrameDelegate alloc] initWithHandler:std::move(on_frame)];
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  impl_->observers = @[
    [center addObserverForName:AVCaptureSessionRuntimeErrorNotification
                        object:impl_->session
                         queue:nil
                    usingBlock:^(NSNotification*) {
                      on_session_failed();
                    }],
    [center addObserverForName:AVCaptureDeviceWasDisconnectedNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification* note) {
                      on_disconnected(UniqueId(note));
                    }],
    [center addObserverForName:AVCaptureDeviceWasConnectedNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification* note) {
                      on_connected(UniqueId(note));
                    }],
  ];
}

AvFoundationSession::~AvFoundationSession() {
  for (id<NSObject> observer in impl_->observers) {
    [NSNotificationCenter.defaultCenter removeObserver:observer];
  }
  [impl_->session stopRunning];
  // Lets a frame being handed over finish before the handler goes.
  dispatch_sync(impl_->capture_queue, ^{
                });
}

CaptureSession::Permission AvFoundationSession::CurrentPermission() {
  switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
    case AVAuthorizationStatusAuthorized:
      return Permission::kGranted;
    case AVAuthorizationStatusNotDetermined:
      return Permission::kUndetermined;
    default:
      return Permission::kDenied;
  }
}

void AvFoundationSession::RequestPermission(std::function<void(bool granted)> done) {
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                           completionHandler:^(BOOL granted) {
                             done(granted == YES);
                           }];
}

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;

// Makes the device's first format of `width` x `height` its active one, if it
// has one; otherwise leaves it as it is.
void UseFormat(AVCaptureDevice* device, int width, int height) {
  for (AVCaptureDeviceFormat* format in device.formats) {
    const CMVideoDimensions size = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    if (size.width == width && size.height == height && [device lockForConfiguration:nil]) {
      device.activeFormat = format;
      [device unlockForConfiguration];
      return;
    }
  }
}

}  // namespace

// Builds the session afresh for the default camera, at 640x480 in 32BGRA.
// The preset alone is not enough: Start keeps the device locked across
// startRunning, so the session never applies the preset's format, and the
// camera would run at its default, 1920x1080 on a MacBook's. So the device's
// active format is set to 640x480 too, as AVCaptureDevice's activeFormat
// documentation describes.
std::optional<std::string> AvFoundationSession::Configure() {
  AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
  if (device == nil) {
    return std::nullopt;
  }
  AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:nil];
  if (input == nil) {
    return std::nullopt;
  }
  AVCaptureSession* session = impl_->session;
  [session beginConfiguration];
  for (AVCaptureInput* old in session.inputs) {
    [session removeInput:old];
  }
  for (AVCaptureOutput* old in session.outputs) {
    [session removeOutput:old];
  }
  session.sessionPreset = AVCaptureSessionPreset640x480;
  const bool added = [session canAddInput:input];
  if (added) {
    [session addInput:input];
    AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
    output.alwaysDiscardsLateVideoFrames = YES;
    output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    [output setSampleBufferDelegate:impl_->delegate queue:impl_->capture_queue];
    [session addOutput:output];
  }
  [session commitConfiguration];
  impl_->device = added ? device : nil;
  if (!added) {
    return std::nullopt;
  }
  UseFormat(device, kWidth, kHeight);
  return std::string(device.uniqueID.UTF8String);
}

// The slowest and fastest rates across the active format's ranges, rounded
// inward so both are supported. None when no device is configured.
CaptureSession::FrameRates AvFoundationSession::SupportedFrameRates() {
  FrameRates rates;
  for (AVFrameRateRange* range in impl_->device.activeFormat.videoSupportedFrameRateRanges) {
    const int slowest = static_cast<int>(std::ceil(range.minFrameRate));
    const int fastest = static_cast<int>(std::floor(range.maxFrameRate));
    rates.min = rates.max == 0 ? slowest : std::min(rates.min, slowest);
    rates.max = std::max(rates.max, fastest);
  }
  return rates;
}

// Starts the session at `frames_per_second`, where the device's format allows
// it. The session resets the device's frame duration when it starts, so the
// device stays locked for configuration across startRunning, as Apple's
// AVCaptureDevice documentation describes.
bool AvFoundationSession::Start(int frames_per_second, std::uint64_t attempt) {
  [impl_->delegate setAttempt:attempt];
  AVCaptureDevice* device = impl_->device;
  const bool locked = [device lockForConfiguration:nil];
  if (locked) {
    const CMTime duration = CMTimeMake(1, frames_per_second);
    for (AVFrameRateRange* range in device.activeFormat.videoSupportedFrameRateRanges) {
      if (CMTimeCompare(duration, range.minFrameDuration) >= 0 &&
          CMTimeCompare(duration, range.maxFrameDuration) <= 0) {
        device.activeVideoMinFrameDuration = duration;
        device.activeVideoMaxFrameDuration = duration;
        break;
      }
    }
  }
  [impl_->session startRunning];
  if (locked) {
    [device unlockForConfiguration];
  }
  return impl_->session.running;
}

// Stops the session, then lets a frame already handed to the capture queue
// finish while the attempt it was captured under is still the delegate's.
// Without the wait, a callback delayed across a close and reopen would stamp
// its old pixels with the next attempt (specs/CameraLifecycle.tla's Capture
// stamps the frame at capture, #40). The destructor waits the same way.
void AvFoundationSession::Stop() {
  [impl_->session stopRunning];
  dispatch_sync(impl_->capture_queue, ^{
                });
}

}  // namespace facepalm
