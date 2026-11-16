#include "src/macos/avfoundation_camera.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

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

struct AvFoundationCamera::Impl {
  dispatch_queue_t control;
  CameraReports& reports;
  Options options;
  // Session work blocks (startRunning does), so it has a queue of its own.
  dispatch_queue_t session_queue =
      dispatch_queue_create("facepalm.camera.session", DISPATCH_QUEUE_SERIAL);
  dispatch_queue_t capture_queue =
      dispatch_queue_create("facepalm.camera.capture", DISPATCH_QUEUE_SERIAL);
  AVCaptureSession* session = [[AVCaptureSession alloc] init];
  FacepalmFrameDelegate* delegate;
  NSArray<id<NSObject>>* observers = @[];

  // Runs one report on the control queue.
  void Report(void (CameraReports::*report)()) {
    CameraReports* target = &reports;
    dispatch_async(control, ^{
      (target->*report)();
    });
  }

  // Builds the session afresh for the default camera, at 640x480 and the
  // chosen frame rate. Returns false when there is no camera to use.
  bool Configure() {
    AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (device == nil) {
      return false;
    }
    NSError* error = nil;
    AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
    if (input == nil) {
      return false;
    }
    [session beginConfiguration];
    for (AVCaptureInput* old in session.inputs) {
      [session removeInput:old];
    }
    for (AVCaptureOutput* old in session.outputs) {
      [session removeOutput:old];
    }
    session.sessionPreset = AVCaptureSessionPreset640x480;
    if (![session canAddInput:input]) {
      [session commitConfiguration];
      return false;
    }
    [session addInput:input];
    AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
    output.alwaysDiscardsLateVideoFrames = YES;
    output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    [output setSampleBufferDelegate:delegate queue:capture_queue];
    [session addOutput:output];
    [session commitConfiguration];
    SetFrameRate(device);
    return true;
  }

  // Sets the frame rate where the device's active format supports it.
  void SetFrameRate(AVCaptureDevice* device) const {
    const CMTime duration = CMTimeMake(1, options.frames_per_second);
    for (AVFrameRateRange* range in device.activeFormat.videoSupportedFrameRateRanges) {
      if (CMTimeCompare(duration, range.minFrameDuration) >= 0 &&
          CMTimeCompare(duration, range.maxFrameDuration) <= 0 &&
          [device lockForConfiguration:nil]) {
        device.activeVideoMinFrameDuration = duration;
        device.activeVideoMaxFrameDuration = duration;
        [device unlockForConfiguration];
        return;
      }
    }
  }

  // Starts the session: Opened when it runs, OpenFailed otherwise.
  void Start() {
    if (!Configure()) {
      Report(&CameraReports::OnOpenFailed);
      return;
    }
    [session startRunning];
    Report(session.running ? &CameraReports::OnOpened : &CameraReports::OnOpenFailed);
  }
};

AvFoundationCamera::AvFoundationCamera(dispatch_queue_t control, CameraReports& reports,
                                       Options options, std::function<void(const Frame&)> on_frame)
    : impl_(std::make_unique<Impl>(
          Impl{.control = control, .reports = reports, .options = options, .delegate = nil})) {
  impl_->delegate = [[FacepalmFrameDelegate alloc] initWithHandler:std::move(on_frame)];
  Impl* impl = impl_.get();
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  // A runtime error or a disconnect is the spec's DeviceLost: the session has
  // stopped, and the controller treats the camera as closed.
  auto lost = ^(NSNotification*) {
    dispatch_async(impl->session_queue, ^{
      [impl->session stopRunning];
    });
    impl->Report(&CameraReports::OnDeviceLost);
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
                      impl->Report(&CameraReports::OnDeviceBack);
                    }],
  ];
}

AvFoundationCamera::~AvFoundationCamera() {
  for (id<NSObject> observer in impl_->observers) {
    [NSNotificationCenter.defaultCenter removeObserver:observer];
  }
  // Waits for session work in flight, so no block outlives the camera.
  dispatch_sync(impl_->session_queue, ^{
    [impl_->session stopRunning];
  });
  // Lets a frame being handed over finish before the handler goes.
  dispatch_sync(impl_->capture_queue, ^{
                });
  // Without OS_OBJECT_USE_OBJC, ARC does not release dispatch objects.
  dispatch_release(impl_->capture_queue);
  dispatch_release(impl_->session_queue);
}

// specs/CameraLifecycle.tla, Open: asks for camera permission the first time,
// then starts the session. Denied permission is the spec's OpenFailed.
void AvFoundationCamera::Open() {
  Impl* impl = impl_.get();
  dispatch_async(impl->session_queue, ^{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
      case AVAuthorizationStatusAuthorized:
        impl->Start();
        return;
      case AVAuthorizationStatusNotDetermined:
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
                                   dispatch_async(impl->session_queue, ^{
                                     if (granted) {
                                       impl->Start();
                                     } else {
                                       impl->Report(&CameraReports::OnOpenFailed);
                                     }
                                   });
                                 }];
        return;
      default:
        impl->Report(&CameraReports::OnOpenFailed);
    }
  });
}

// specs/CameraLifecycle.tla, Closed follows once the session has stopped.
void AvFoundationCamera::Close() {
  Impl* impl = impl_.get();
  dispatch_async(impl->session_queue, ^{
    [impl->session stopRunning];
    impl->Report(&CameraReports::OnClosed);
  });
}

}  // namespace facepalm
