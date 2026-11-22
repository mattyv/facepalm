#include "src/macos/preview_jpeg.h"

#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <optional>
#include <string>

#include "src/frame.h"

namespace facepalm {

// Mirrors tools/camera_probe/camera_probe.cc's PngSink::SaveImage, but
// encodes to JPEG in memory rather than PNG to a file.
std::optional<std::string> EncodePreviewJpeg(const Frame& frame, float quality) {
  constexpr std::size_t kBitsPerChannel = 8;
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef context = CGBitmapContextCreate(
      const_cast<std::uint8_t*>(frame.bgra), static_cast<std::size_t>(frame.width),
      static_cast<std::size_t>(frame.height), kBitsPerChannel, frame.bytes_per_row, space,
      static_cast<std::uint32_t>(kCGImageAlphaNoneSkipFirst) |
          static_cast<std::uint32_t>(kCGBitmapByteOrder32Little));
  CGColorSpaceRelease(space);
  if (context == nullptr) {
    return std::nullopt;
  }
  // A context CGBitmapContextCreate has already accepted always yields an
  // image, and a mutable CFData and a "public.jpeg" destination (a type
  // ImageIO always supports) are not input-dependent failures either, so
  // only CGImageDestinationFinalize, the actual encode, is checked: the
  // one step a pathological frame, not just a zero-sized one already
  // refused above, could still fail.
  CGImageRef image = CGBitmapContextCreateImage(context);
  CGContextRelease(context);
  CFMutableDataRef data = CFDataCreateMutable(nullptr, 0);
  CGImageDestinationRef destination =
      CGImageDestinationCreateWithData(data, CFSTR("public.jpeg"), 1, nullptr);
  NSDictionary* options = @{(NSString*)kCGImageDestinationLossyCompressionQuality : @(quality)};
  CGImageDestinationAddImage(destination, image, (CFDictionaryRef)options);
  std::optional<std::string> encoded;
  if (CGImageDestinationFinalize(destination)) {
    encoded = std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                          static_cast<std::size_t>(CFDataGetLength(data)));
  }
  CFRelease(destination);
  CFRelease(data);
  CGImageRelease(image);
  return encoded;
}

}  // namespace facepalm
