// Runs one image through MediaPipeFaceModel and the real
// libfacepalm_mediapipe.dylib, and prints the face as face_landmarker_fixtures
// does, so tools/fixtures/check_portrait.sh holds it to MediaPipe's expected
// results. Built only when FACEPALM_MEDIAPIPE_DYLIB names the dylib.
//
// Usage: face_model_check <model.task> <image>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "src/face_model.h"

namespace {

constexpr std::size_t kChannels = 4;
constexpr std::size_t kBitsPerChannel = 8;

struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

// Decodes `path` into packed RGBA with ImageIO, in the image's own colour
// space, so the pixels are the file's, as MediaPipe's decoder reads them.
std::optional<Image> Decode(const char* path) {
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      nullptr, reinterpret_cast<const UInt8*>(path),
      static_cast<CFIndex>(std::strlen(path)),
      /*isDirectory=*/static_cast<Boolean>(false));
  CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
  CFRelease(url);
  if (source == nullptr) {
    return std::nullopt;
  }
  CGImageRef cg_image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (cg_image == nullptr) {
    return std::nullopt;
  }
  Image image{.width = static_cast<int>(CGImageGetWidth(cg_image)),
              .height = static_cast<int>(CGImageGetHeight(cg_image)),
              .rgba = {}};
  image.rgba.resize(static_cast<std::size_t>(image.width) *
                    static_cast<std::size_t>(image.height) * kChannels);
  CGContextRef context = CGBitmapContextCreate(
      image.rgba.data(), CGImageGetWidth(cg_image), CGImageGetHeight(cg_image),
      kBitsPerChannel, CGImageGetWidth(cg_image) * kChannels,
      CGImageGetColorSpace(cg_image),
      static_cast<std::uint32_t>(kCGImageAlphaPremultipliedLast) |
          static_cast<std::uint32_t>(kCGBitmapByteOrder32Big));
  if (context != nullptr) {
    CGContextDrawImage(context, CGRectMake(0, 0, image.width, image.height),
                       cg_image);
    CGContextRelease(context);
  }
  CGImageRelease(cg_image);
  if (context == nullptr) {
    return std::nullopt;
  }
  return image;
}

// Prints the face in face_landmarker_fixtures' JSON, with the blendshapes
// keyed by index, since the shim carries their scores without names.
void Print(const char* path, const facepalm::Face& face) {
  std::printf("[{\"image\": \"%s\",\n \"landmarks\": [", path);
  for (int i = 0; i < FP_LANDMARKS; ++i) {
    std::printf("%s[%.9g, %.9g, %.9g]", i == 0 ? "" : ", ",
                face.landmarks[i][0], face.landmarks[i][1],
                face.landmarks[i][2]);
  }
  std::printf("],\n \"blendshapes\": {");
  for (int i = 0; i < FP_BLENDSHAPES; ++i) {
    std::printf("%s\"%d\": %.9g", i == 0 ? "" : ", ", i, face.blendshapes[i]);
  }
  std::printf("},\n \"pose_column_major\": [");
  for (int i = 0; i < FP_POSE_VALUES; ++i) {
    std::printf("%s%.9g", i == 0 ? "" : ", ", face.pose_column_major[i]);
  }
  std::printf("]}]\n");
}

}  // namespace

// The standard fixes main's signature, and an allocation failure may end the
// tool.
// NOLINTNEXTLINE(misc-const-correctness,bugprone-exception-escape)
int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: face_model_check <model.task> <image>\n");
    return 1;
  }
  const std::optional<Image> image = Decode(argv[2]);
  if (!image) {
    std::fprintf(stderr, "face_model_check: cannot decode %s\n", argv[2]);
    return 1;
  }
  std::string error;
  auto model = facepalm::MediaPipeFaceModel::Create(argv[1], error);
  if (model == nullptr) {
    std::fprintf(stderr, "face_model_check: %s\n", error.c_str());
    return 1;
  }
  facepalm::Face face{};
  switch (model->Detect(image->rgba, image->width, image->height,
                        /*timestamp_ms=*/0, face, error)) {
    case facepalm::FaceModel::Result::kFace:
      Print(argv[2], face);
      return 0;
    case facepalm::FaceModel::Result::kNoFace:
      std::fprintf(stderr, "face_model_check: no face in %s\n", argv[2]);
      return 1;
    case facepalm::FaceModel::Result::kFailed:
      std::fprintf(stderr, "face_model_check: %s\n", error.c_str());
      return 1;
  }
  return 1;
}
