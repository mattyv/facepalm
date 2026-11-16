#include "src/macos/avfoundation_session.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

// Hands each sample buffer to a C++ callback, as MediaPipe's
// MPPCameraInputSource is its own sample buffer delegate.
@interface FacepalmFrameDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (instancetype)initWithHandler:(std::function<void(const facepalm::Frame&)>)handler;
@end

@implementation FacepalmFrameDelegate {
  std::function<void(const facepalm::Frame&)> _handler;
}

- (instancetype)initWithHandler:(std::function<void(const facepalm::Frame&)>)handler {
  if ((self = [super init])) {
    _handler = std::move(handler);
  }
  return self;
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
  };
  _handler(frame);
  CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
}

@end

namespace facepalm {

struct AvFoundationSession::Impl {
  AVCaptureSession* session = [[AVCaptureSession alloc] init];
  dispatch_queue_t capture_queue =
      dispatch_queue_create("facepalm.camera.capture", DISPATCH_QUEUE_SERIAL);
  FacepalmFrameDelegate* delegate = nil;
  AVCaptureDevice* device = nil;
  NSArray<id<NSObject>>* observers = @[];
};

AvFoundationSession::AvFoundationSession(std::function<void(const Frame&)> on_frame,
                                         std::function<void()> on_device_lost,
                                         std::function<void()> on_device_back)
    : impl_(std::make_unique<Impl>()) {
  impl_->delegate = [[FacepalmFrameDelegate alloc] initWithHandler:std::move(on_frame)];
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  auto lost = ^(NSNotification*) {
    on_device_lost();
  };
  impl_->observers = @[
    [center addObserverForName:AVCaptureSessionRuntimeErrorNotification
                        object:impl_->session
                         queue:nil
                    usingBlock:lost],
    [center addObserverForName:AVCaptureDeviceWasDisconnectedNotification
                        object:nil
                         queue:nil
                    usingBlock:lost],
    [center addObserverForName:AVCaptureDeviceWasConnectedNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification*) {
                      on_device_back();
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

// Builds the session afresh for the default camera, at 640x480 in 32BGRA.
bool AvFoundationSession::Configure() {
  AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
  if (device == nil) {
    return false;
  }
  AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:nil];
  if (input == nil) {
    return false;
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
  return added;
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
bool AvFoundationSession::Start(int frames_per_second) {
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

void AvFoundationSession::Stop() { [impl_->session stopRunning]; }

}  // namespace facepalm
