#include "src/face_model.h"

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
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;

// The shim's C functions, as a mock. The fakes below forward to it, standing
// in for libfacepalm_mediapipe.dylib at link time.
class MockShim {
 public:
  MOCK_METHOD(int, Create,
              (const char* model_path, FpFaceModel** out, char* error,
               std::size_t error_size));
  MOCK_METHOD(int, Detect,
              (FpFaceModel * model, const std::uint8_t* rgba, int width,
               int height, std::int64_t timestamp_ms, FpFace* out, char* error,
               std::size_t error_size));
  MOCK_METHOD(void, Destroy, (FpFaceModel * model));
};

MockShim* shim = nullptr;

// Writes `message` into a shim error buffer, as the shim does.
int Fail(const char* message, char* error, std::size_t error_size) {
  std::strncpy(error, message, error_size - 1);
  error[error_size - 1] = '\0';
  return -1;
}

// Stands in for the real model's handle; never dereferenced.
FpFaceModel* const fake_handle = reinterpret_cast<FpFaceModel*>(0x1234);

constexpr int kWidth = 2;
constexpr int kHeight = 1;

class MediaPipeFaceModelTest : public ::testing::Test {
 protected:
  void SetUp() override { shim = &mock_; }
  void TearDown() override { shim = nullptr; }

  // Creates a model over fake_handle, which is destroyed with it.
  std::unique_ptr<MediaPipeFaceModel> Loaded() {
    EXPECT_CALL(mock_, Create(_, _, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_handle), Return(0)));
    EXPECT_CALL(mock_, Destroy(fake_handle));
    std::string error;
    return MediaPipeFaceModel::Create("model.task", error);
  }

  ::testing::StrictMock<MockShim> mock_;
  std::vector<std::uint8_t> rgba_ = std::vector<std::uint8_t>(
      static_cast<std::size_t>(kWidth * kHeight * 4), 0);
  Face face_{};
  std::string error_;
};

TEST_F(MediaPipeFaceModelTest, CreatePassesThePathAndKeepsTheModel) {
  EXPECT_CALL(mock_, Create(StrEq("face.task"), NotNull(), NotNull(), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_handle), Return(0)));
  EXPECT_CALL(mock_, Destroy(fake_handle));
  std::string error;

  EXPECT_THAT(MediaPipeFaceModel::Create("face.task", error), NotNull());
  EXPECT_THAT(error, Eq(""));
}

TEST_F(MediaPipeFaceModelTest, CreateFailingGivesNullAndTheShimsReason) {
  EXPECT_CALL(mock_, Create(_, _, _, _))
      .WillOnce([](const char*, FpFaceModel**, char* error, std::size_t size) {
        return Fail("no such model", error, size);
      });
  std::string error;

  EXPECT_THAT(MediaPipeFaceModel::Create("missing.task", error), IsNull());
  EXPECT_THAT(error, Eq("no such model"));
}

TEST_F(MediaPipeFaceModelTest, DetectPassesTheFrameAndReturnsTheFace) {
  auto model = Loaded();
  EXPECT_CALL(mock_, Detect(fake_handle, rgba_.data(), kWidth, kHeight, 33,
                            NotNull(), NotNull(), _))
      .WillOnce([](FpFaceModel*, const std::uint8_t*, int, int, std::int64_t,
                   FpFace* out, char*, std::size_t) {
        out->pose_column_major[0] = 0.5F;
        return 1;
      });

  EXPECT_THAT(model->Detect(rgba_, kWidth, kHeight, 33, face_, error_),
              Eq(FaceModel::Result::kFace));
  EXPECT_THAT(face_.pose_column_major[0], Eq(0.5F));
}

TEST_F(MediaPipeFaceModelTest, DetectWithNoFaceSaysSo) {
  auto model = Loaded();
  EXPECT_CALL(mock_, Detect(_, _, _, _, _, _, _, _)).WillOnce(Return(0));

  EXPECT_THAT(model->Detect(rgba_, kWidth, kHeight, 0, face_, error_),
              Eq(FaceModel::Result::kNoFace));
}

TEST_F(MediaPipeFaceModelTest, DetectFailingGivesTheShimsReason) {
  auto model = Loaded();
  EXPECT_CALL(mock_, Detect(_, _, _, _, _, _, _, _))
      .WillOnce([](FpFaceModel*, const std::uint8_t*, int, int, std::int64_t,
                   FpFace*, char* error, std::size_t size) {
        return Fail("timestamp went backwards", error, size);
      });

  EXPECT_THAT(model->Detect(rgba_, kWidth, kHeight, 0, face_, error_),
              Eq(FaceModel::Result::kFailed));
  EXPECT_THAT(error_, Eq("timestamp went backwards"));
}

TEST_F(MediaPipeFaceModelTest, DetectRefusesABufferOfTheWrongSize) {
  auto model = Loaded();
  rgba_.pop_back();

  EXPECT_THAT(model->Detect(rgba_, kWidth, kHeight, 0, face_, error_),
              Eq(FaceModel::Result::kFailed));
  EXPECT_THAT(error_, Eq("expected 8 bytes of RGBA for 2x1, got 7"));
}

TEST_F(MediaPipeFaceModelTest, DetectRefusesAFrameWithNoWidth) {
  auto model = Loaded();
  rgba_.clear();

  EXPECT_THAT(model->Detect(rgba_, 0, kHeight, 0, face_, error_),
              Eq(FaceModel::Result::kFailed));
}

TEST_F(MediaPipeFaceModelTest, DetectRefusesAFrameWithNoHeight) {
  auto model = Loaded();
  rgba_.clear();

  EXPECT_THAT(model->Detect(rgba_, kWidth, 0, 0, face_, error_),
              Eq(FaceModel::Result::kFailed));
}

}  // namespace
}  // namespace facepalm

// The link-time seam: the shim's interface, forwarding to the mock.
extern "C" int fp_face_model_create(const char* model_path, FpFaceModel** out,
                                    char* error, size_t error_size) {
  return facepalm::shim->Create(model_path, out, error, error_size);
}

extern "C" int fp_face_model_detect(FpFaceModel* model, const uint8_t* rgba,
                                    int width, int height, int64_t timestamp_ms,
                                    FpFace* out, char* error,
                                    size_t error_size) {
  return facepalm::shim->Detect(model, rgba, width, height, timestamp_ms, out,
                                error, error_size);
}

extern "C" void fp_face_model_destroy(FpFaceModel* model) {
  facepalm::shim->Destroy(model);
}
