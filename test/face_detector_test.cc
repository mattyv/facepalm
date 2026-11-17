#include "src/face_detector.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;

// The shim's C functions, as a mock. The fakes below forward to it, standing
// in for libfacepalm_mediapipe.dylib at link time.
class MockShim {
 public:
  MOCK_METHOD(int, Create,
              (const char* model_path, FpFaceDetector** out, char* error,
               std::size_t error_size));
  MOCK_METHOD(int, Detect,
              (FpFaceDetector * detector, const std::uint8_t* rgba, int width,
               int height, FpDetection* out, char* error,
               std::size_t error_size));
  MOCK_METHOD(void, Destroy, (FpFaceDetector * detector));
};

MockShim* shim = nullptr;

// Writes `message` into a shim error buffer, as the shim does.
int Fail(const char* message, char* error, std::size_t error_size) {
  std::strncpy(error, message, error_size - 1);
  error[error_size - 1] = '\0';
  return -1;
}

// Stands in for the real detector's handle; never dereferenced.
FpFaceDetector* const fake_handle = reinterpret_cast<FpFaceDetector*>(0x1234);

constexpr int kWidth = 2;
constexpr int kHeight = 1;

class MediaPipeFaceDetectorTest : public ::testing::Test {
 protected:
  void SetUp() override { shim = &mock_; }
  void TearDown() override { shim = nullptr; }

  // Creates a detector over fake_handle, which is destroyed with it.
  std::unique_ptr<MediaPipeFaceDetector> Loaded() {
    EXPECT_CALL(mock_, Create(_, _, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_handle), Return(0)));
    EXPECT_CALL(mock_, Destroy(fake_handle));
    std::string error;
    return MediaPipeFaceDetector::Create("detector.tflite", error);
  }

  ::testing::StrictMock<MockShim> mock_;
  std::vector<std::uint8_t> rgba_ = std::vector<std::uint8_t>(
      static_cast<std::size_t>(kWidth * kHeight * 4), 0);
  Detection detection_{};
  std::string error_;
};

TEST_F(MediaPipeFaceDetectorTest, CreatePassesThePathAndKeepsTheDetector) {
  EXPECT_CALL(mock_, Create(StrEq("short.tflite"), NotNull(), NotNull(), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_handle), Return(0)));
  EXPECT_CALL(mock_, Destroy(fake_handle));
  std::string error;

  EXPECT_THAT(MediaPipeFaceDetector::Create("short.tflite", error), NotNull());
  EXPECT_THAT(error, Eq(""));
}

TEST_F(MediaPipeFaceDetectorTest, CreateFailingGivesNullAndTheShimsReason) {
  EXPECT_CALL(mock_, Create(_, _, _, _))
      .WillOnce(
          [](const char*, FpFaceDetector**, char* error, std::size_t size) {
            return Fail("no such model", error, size);
          });
  std::string error;

  EXPECT_THAT(MediaPipeFaceDetector::Create("missing.tflite", error), IsNull());
  EXPECT_THAT(error, Eq("no such model"));
}

TEST_F(MediaPipeFaceDetectorTest, DetectPassesTheFrameAndCountsTheFaces) {
  auto detector = Loaded();
  EXPECT_CALL(mock_, Detect(fake_handle, rgba_.data(), kWidth, kHeight,
                            NotNull(), NotNull(), _))
      .WillOnce([](FpFaceDetector*, const std::uint8_t*, int, int,
                   FpDetection* out, char*, std::size_t) {
        out->box[2] = 0.5F;
        return 2;
      });

  EXPECT_THAT(detector->Detect(rgba_, kWidth, kHeight, detection_, error_),
              Optional(2));
  EXPECT_THAT(detection_.box[2], Eq(0.5F));
}

TEST_F(MediaPipeFaceDetectorTest, DetectFailingGivesTheShimsReason) {
  auto detector = Loaded();
  EXPECT_CALL(mock_, Detect(_, _, _, _, _, _, _))
      .WillOnce([](FpFaceDetector*, const std::uint8_t*, int, int, FpDetection*,
                   char* error, std::size_t size) {
        return Fail("bad frame", error, size);
      });

  EXPECT_THAT(detector->Detect(rgba_, kWidth, kHeight, detection_, error_),
              Eq(std::nullopt));
  EXPECT_THAT(error_, Eq("bad frame"));
}

TEST_F(MediaPipeFaceDetectorTest, DetectRefusesABufferOfTheWrongSize) {
  auto detector = Loaded();
  rgba_.pop_back();

  EXPECT_THAT(detector->Detect(rgba_, kWidth, kHeight, detection_, error_),
              Eq(std::nullopt));
  EXPECT_THAT(error_, Eq("expected 8 bytes of RGBA for 2x1, got 7"));
}

TEST_F(MediaPipeFaceDetectorTest, DetectRefusesAFrameWithNoWidth) {
  auto detector = Loaded();
  rgba_.clear();

  EXPECT_THAT(detector->Detect(rgba_, 0, kHeight, detection_, error_),
              Eq(std::nullopt));
}

TEST_F(MediaPipeFaceDetectorTest, DetectRefusesAFrameWithNoHeight) {
  auto detector = Loaded();
  rgba_.clear();

  EXPECT_THAT(detector->Detect(rgba_, kWidth, 0, detection_, error_),
              Eq(std::nullopt));
}

}  // namespace
}  // namespace facepalm

// The link-time seam: the shim's interface, forwarding to the mock.
extern "C" int fp_face_detector_create(const char* model_path,
                                       FpFaceDetector** out, char* error,
                                       size_t error_size) {
  return facepalm::shim->Create(model_path, out, error, error_size);
}

extern "C" int fp_face_detector_detect(FpFaceDetector* detector,
                                       const uint8_t* rgba, int width,
                                       int height, FpDetection* out,
                                       char* error, size_t error_size) {
  return facepalm::shim->Detect(detector, rgba, width, height, out, error,
                                error_size);
}

extern "C" void fp_face_detector_destroy(FpFaceDetector* detector) {
  facepalm::shim->Destroy(detector);
}
