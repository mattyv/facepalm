// Tests src/linux/mediapipe_c_shim.cc with MediaPipe's C API mocked: the
// fakes at the end of this file stand in for libmediapipe.so at link time,
// as test/face_model_test.cc's stand in for the shim (#83).

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h"
#include "src/mediapipe_shim/facepalm_mediapipe.h"

namespace facepalm {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;

// The MediaPipe C functions the shim calls, as a mock.
class MockMediaPipe {
 public:
  MOCK_METHOD(MpStatus, Create,
              (FaceLandmarkerOptions * options, MpFaceLandmarkerPtr* landmarker,
               char** error));
  MOCK_METHOD(MpStatus, CreateImage,
              (MpImageFormat format, int width, int height,
               const std::uint8_t* pixels, int size, MpImagePtr* out,
               char** error));
  MOCK_METHOD(MpStatus, DetectForVideo,
              (MpFaceLandmarkerPtr landmarker, MpImagePtr image,
               const ImageProcessingOptions* options, std::int64_t timestamp_ms,
               FaceLandmarkerResult* result, char** error));
  MOCK_METHOD(void, FreeImage, (MpImagePtr image));
  MOCK_METHOD(void, CloseResult, (FaceLandmarkerResult * result));
  MOCK_METHOD(MpStatus, Close, (MpFaceLandmarkerPtr landmarker, char** error));
  MOCK_METHOD(void, FreeError, (char* error));
};

MockMediaPipe* media_pipe = nullptr;

// Handles that stand in for MediaPipe's own; never dereferenced.
MpFaceLandmarkerPtr const fake_landmarker =
    reinterpret_cast<MpFaceLandmarkerPtr>(0x1234);
MpImagePtr const fake_image = reinterpret_cast<MpImagePtr>(0x5678);

constexpr int kWidth = 2;
constexpr int kHeight = 3;
constexpr std::size_t kErrorSize = 64;

// One face as MediaPipe's C API returns it, which the shim copies from.
struct FakeResult {
  FakeResult() {
    for (std::size_t i = 0; i < landmarks.size(); ++i) {
      const auto value = static_cast<float>(i);
      landmarks[i] = {.x = value, .y = value + 0.25F, .z = value + 0.5F};
    }
    for (std::size_t i = 0; i < categories.size(); ++i) {
      categories[i].score = static_cast<float>(i) / 100;
    }
    for (std::size_t i = 0; i < matrix.size(); ++i) {
      matrix[i] = static_cast<float>(i) * 2;
    }
  }

  // The result over this face's arrays, with every count as a real model's.
  FaceLandmarkerResult Result() {
    return {.face_landmarks = &face_landmarks,
            .face_landmarks_count = 1,
            .face_blendshapes = &face_blendshapes,
            .face_blendshapes_count = 1,
            .facial_transformation_matrixes = &face_matrix,
            .facial_transformation_matrixes_count = 1};
  }

  std::array<NormalizedLandmark, FP_LANDMARKS> landmarks{};
  std::array<Category, FP_BLENDSHAPES> categories{};
  std::array<float, FP_POSE_VALUES> matrix{};
  NormalizedLandmarks face_landmarks{.landmarks = landmarks.data(),
                                     .landmarks_count = FP_LANDMARKS};
  Categories face_blendshapes{.categories = categories.data(),
                              .categories_count = FP_BLENDSHAPES};
  Matrix face_matrix{.rows = 4, .cols = 4, .data = matrix.data()};
};

class MediaPipeCShimTest : public ::testing::Test {
 protected:
  void SetUp() override { media_pipe = &mock_; }
  void TearDown() override { media_pipe = nullptr; }

  // Creates a model over fake_landmarker.
  FpFaceModel* Created() {
    EXPECT_CALL(mock_, Create(_, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(fake_landmarker), Return(kMpOk)));
    FpFaceModel* model = nullptr;
    EXPECT_THAT(
        fp_face_model_create("face.task", &model, error_.data(), error_.size()),
        Eq(0));
    return model;
  }

  // Destroys a model, closing its landmarker without an error.
  void Destroy(FpFaceModel* model) {
    EXPECT_CALL(mock_, Close(fake_landmarker, NotNull()))
        .WillOnce(Return(kMpOk));
    fp_face_model_destroy(model);
  }

  // Expects one frame turned into fake_image and freed after detection.
  void ExpectImage() {
    EXPECT_CALL(mock_, CreateImage(_, _, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(fake_image), Return(kMpOk)));
    EXPECT_CALL(mock_, FreeImage(fake_image));
  }

  // Detects in one frame, which MediaPipe answers with `result`.
  int Detect(FpFaceModel* model, const FaceLandmarkerResult& result) {
    ExpectImage();
    EXPECT_CALL(mock_, DetectForVideo(fake_landmarker, fake_image, _, _,
                                      NotNull(), NotNull()))
        .WillOnce(DoAll(SetArgPointee<4>(result), Return(kMpOk)));
    EXPECT_CALL(mock_, CloseResult(NotNull()));
    return fp_face_model_detect(model, rgba_.data(), kWidth, kHeight, 0, &face_,
                                error_.data(), error_.size());
  }

  // The shim's error message, up to its terminator.
  std::string Error() const { return error_.data(); }

  ::testing::StrictMock<MockMediaPipe> mock_;
  std::array<char, kErrorSize> error_{};
  std::vector<std::uint8_t> rgba_ =
      std::vector<std::uint8_t>(std::size_t{kWidth} * kHeight * 4, 0);
  FpFace face_{};
  FakeResult fake_;
};

TEST_F(MediaPipeCShimTest, CreateLoadsTheModelForVideoWithEveryOutput) {
  FaceLandmarkerOptions options{};
  EXPECT_CALL(mock_, Create(NotNull(), NotNull(), NotNull()))
      .WillOnce([&options](FaceLandmarkerOptions* given,
                           MpFaceLandmarkerPtr* landmarker, char**) {
        options = *given;
        *landmarker = fake_landmarker;
        return kMpOk;
      });
  FpFaceModel* model = nullptr;

  EXPECT_THAT(
      fp_face_model_create("face.task", &model, error_.data(), error_.size()),
      Eq(0));
  EXPECT_THAT(model, NotNull());
  EXPECT_THAT(options.base_options.model_asset_path, StrEq("face.task"));
  EXPECT_THAT(options.base_options.host_system, Eq(HOST_SYSTEM_LINUX));
  EXPECT_THAT(options.running_mode, Eq(VIDEO));
  EXPECT_TRUE(options.output_face_blendshapes);
  EXPECT_TRUE(options.output_facial_transformation_matrixes);
  EXPECT_THAT(options.num_faces, Eq(1));
  Destroy(model);
}

TEST_F(MediaPipeCShimTest, CreateFailingGivesMediaPipesReasonAndFreesIt) {
  char reason[] = "no such model";
  EXPECT_CALL(mock_, Create(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(reason), Return(kMpNotFound)));
  EXPECT_CALL(mock_, FreeError(reason));
  FpFaceModel* model = nullptr;

  EXPECT_THAT(
      fp_face_model_create("face.task", &model, error_.data(), error_.size()),
      Eq(-1));
  EXPECT_THAT(Error(), Eq("no such model"));
  EXPECT_THAT(model, Eq(nullptr));
}

TEST_F(MediaPipeCShimTest, CreateFailingWithoutAReasonSaysMediaPipeFailed) {
  EXPECT_CALL(mock_, Create(_, _, _)).WillOnce(Return(kMpInternal));
  FpFaceModel* model = nullptr;

  EXPECT_THAT(
      fp_face_model_create("face.task", &model, error_.data(), error_.size()),
      Eq(-1));
  EXPECT_THAT(Error(), Eq("MediaPipe failed"));
}

TEST_F(MediaPipeCShimTest, AFailureWritesNoErrorWithoutABuffer) {
  EXPECT_CALL(mock_, Create(_, _, _)).WillOnce(Return(kMpInternal));
  FpFaceModel* model = nullptr;

  EXPECT_THAT(fp_face_model_create("face.task", &model, nullptr, kErrorSize),
              Eq(-1));
}

TEST_F(MediaPipeCShimTest, AFailureWritesNoErrorIntoAnEmptyBuffer) {
  EXPECT_CALL(mock_, Create(_, _, _)).WillOnce(Return(kMpInternal));
  FpFaceModel* model = nullptr;
  error_[0] = 'x';

  EXPECT_THAT(fp_face_model_create("face.task", &model, error_.data(), 0),
              Eq(-1));
  EXPECT_THAT(error_[0], Eq('x'));
}

TEST_F(MediaPipeCShimTest, AFailureCutsItsErrorToTheBuffer) {
  EXPECT_CALL(mock_, Create(_, _, _)).WillOnce(Return(kMpInternal));
  FpFaceModel* model = nullptr;

  EXPECT_THAT(fp_face_model_create("face.task", &model, error_.data(), 4),
              Eq(-1));
  EXPECT_THAT(Error(), Eq("Med"));
}

TEST_F(MediaPipeCShimTest, DetectPassesTheFrameAsRgbaAndCopiesTheFace) {
  FpFaceModel* model = Created();
  EXPECT_CALL(mock_,
              CreateImage(kMpImageFormatSrgba, kWidth, kHeight, rgba_.data(),
                          kWidth * kHeight * 4, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<5>(fake_image), Return(kMpOk)));
  EXPECT_CALL(mock_, FreeImage(fake_image));
  EXPECT_CALL(mock_, DetectForVideo(fake_landmarker, fake_image, Eq(nullptr),
                                    33, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<4>(fake_.Result()), Return(kMpOk)));
  EXPECT_CALL(mock_, CloseResult(NotNull()));
  ::testing::internal::CaptureStderr();

  EXPECT_THAT(fp_face_model_detect(model, rgba_.data(), kWidth, kHeight, 33,
                                   &face_, error_.data(), error_.size()),
              Eq(1));
  EXPECT_THAT(::testing::internal::GetCapturedStderr(),
              Eq("facepalm-daemon: first camera frame analyzed\n"));
  EXPECT_THAT(face_.landmarks[0][0], Eq(0.0F));
  EXPECT_THAT(face_.landmarks[FP_LANDMARKS - 1][0],
              Eq(static_cast<float>(FP_LANDMARKS - 1)));
  EXPECT_THAT(face_.landmarks[2][1], Eq(2.25F));
  EXPECT_THAT(face_.landmarks[2][2], Eq(2.5F));
  EXPECT_THAT(face_.blendshapes[FP_BLENDSHAPES - 1],
              Eq(static_cast<float>(FP_BLENDSHAPES - 1) / 100));
  EXPECT_THAT(face_.pose_column_major[FP_POSE_VALUES - 1],
              Eq(static_cast<float>(FP_POSE_VALUES - 1) * 2));
  Destroy(model);
}

TEST_F(MediaPipeCShimTest, OnlyTheFirstAnalyzedFrameIsAnnounced) {
  FpFaceModel* model = Created();
  ::testing::internal::CaptureStderr();
  EXPECT_THAT(Detect(model, {}), Eq(0));
  EXPECT_THAT(::testing::internal::GetCapturedStderr(), Not(IsEmpty()));
  ::testing::internal::CaptureStderr();

  EXPECT_THAT(Detect(model, fake_.Result()), Eq(1));
  EXPECT_THAT(::testing::internal::GetCapturedStderr(), IsEmpty());
  Destroy(model);
}

TEST_F(MediaPipeCShimTest, DetectWithNoFaceReturnsZero) {
  FpFaceModel* model = Created();
  ::testing::internal::CaptureStderr();

  EXPECT_THAT(Detect(model, {}), Eq(0));
  static_cast<void>(::testing::internal::GetCapturedStderr());
  Destroy(model);
}

// A result whose counts or shape no single-face model returns.
struct BadShape {
  std::string name;
  std::uint32_t faces = 1;
  std::uint32_t blendshape_lists = 1;
  std::uint32_t matrixes = 1;
  std::uint32_t landmarks = FP_LANDMARKS;
  std::uint32_t blendshapes = FP_BLENDSHAPES;
  std::uint32_t rows = 4;
  std::uint32_t cols = 4;
  std::string error;

  friend void PrintTo(const BadShape& shape, std::ostream* out) {
    *out << shape.name;
  }
};

class BadShapeTest : public MediaPipeCShimTest,
                     public ::testing::WithParamInterface<BadShape> {};

TEST_P(BadShapeTest, DetectRefusesIt) {
  const BadShape& shape = GetParam();
  FpFaceModel* model = Created();
  FaceLandmarkerResult result = fake_.Result();
  result.face_landmarks_count = shape.faces;
  result.face_blendshapes_count = shape.blendshape_lists;
  result.facial_transformation_matrixes_count = shape.matrixes;
  fake_.face_landmarks.landmarks_count = shape.landmarks;
  fake_.face_blendshapes.categories_count = shape.blendshapes;
  fake_.face_matrix.rows = shape.rows;
  fake_.face_matrix.cols = shape.cols;

  EXPECT_THAT(Detect(model, result), Eq(-1));
  EXPECT_THAT(Error(), Eq(shape.error));
  Destroy(model);
}

constexpr char kBadCount[] = "unexpected MediaPipe face count";
constexpr char kBadShape[] = "unexpected MediaPipe result shape";

INSTANTIATE_TEST_SUITE_P(
    EachCheck, BadShapeTest,
    ::testing::Values(
        BadShape{.name = "TwoFaces", .faces = 2, .error = kBadCount},
        BadShape{
            .name = "NoBlendshapes", .blendshape_lists = 0, .error = kBadCount},
        BadShape{.name = "NoMatrix", .matrixes = 0, .error = kBadCount},
        BadShape{.name = "NoIrises", .landmarks = 468, .error = kBadShape},
        BadShape{.name = "TooFewBlendshapes",
                 .blendshapes = FP_BLENDSHAPES - 1,
                 .error = kBadShape},
        BadShape{.name = "ThreeRows", .rows = 3, .error = kBadShape},
        BadShape{.name = "ThreeColumns", .cols = 3, .error = kBadShape}),
    [](const ::testing::TestParamInfo<BadShape>& info) {
      return info.param.name;
    });

TEST_F(MediaPipeCShimTest, DetectFailingGivesMediaPipesReasonAndClosesAll) {
  FpFaceModel* model = Created();
  char reason[] = "timestamp went backwards";
  ExpectImage();
  EXPECT_CALL(mock_, DetectForVideo(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(reason), Return(kMpInvalidArgument)));
  EXPECT_CALL(mock_, FreeError(reason));
  EXPECT_CALL(mock_, CloseResult(NotNull()));
  ::testing::internal::CaptureStderr();

  EXPECT_THAT(fp_face_model_detect(model, rgba_.data(), kWidth, kHeight, 0,
                                   &face_, error_.data(), error_.size()),
              Eq(-1));
  EXPECT_THAT(Error(), Eq("timestamp went backwards"));
  EXPECT_THAT(::testing::internal::GetCapturedStderr(), IsEmpty());
  Destroy(model);
}

TEST_F(MediaPipeCShimTest, DetectGivesMediaPipesReasonWhenNoImageIsMade) {
  FpFaceModel* model = Created();
  char reason[] = "bad pixels";
  EXPECT_CALL(mock_, CreateImage(_, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(reason), Return(kMpInvalidArgument)));
  EXPECT_CALL(mock_, FreeError(reason));

  EXPECT_THAT(fp_face_model_detect(model, rgba_.data(), kWidth, kHeight, 0,
                                   &face_, error_.data(), error_.size()),
              Eq(-1));
  EXPECT_THAT(Error(), Eq("bad pixels"));
  Destroy(model);
}

// Frame sizes the shim refuses before it reaches MediaPipe.
struct BadSize {
  std::string name;
  int width;
  int height;

  friend void PrintTo(const BadSize& size, std::ostream* out) {
    *out << size.name;
  }
};

class BadSizeTest : public MediaPipeCShimTest,
                    public ::testing::WithParamInterface<BadSize> {};

TEST_P(BadSizeTest, DetectRefusesIt) {
  FpFaceModel* model = Created();

  EXPECT_THAT(fp_face_model_detect(model, rgba_.data(), GetParam().width,
                                   GetParam().height, 0, &face_, error_.data(),
                                   error_.size()),
              Eq(-1));
  EXPECT_THAT(Error(), HasSubstr("invalid MediaPipe dimensions"));
  Destroy(model);
}

constexpr int kIntMax = std::numeric_limits<int>::max();

INSTANTIATE_TEST_SUITE_P(
    EachCheck, BadSizeTest,
    ::testing::Values(BadSize{.name = "NoWidth", .width = 0, .height = 1},
                      BadSize{.name = "NoHeight", .width = 1, .height = 0},
                      BadSize{.name = "TooManyBytesForAnInt",
                              .width = kIntMax / 4 + 1,
                              .height = 1}),
    [](const ::testing::TestParamInfo<BadSize>& info) {
      return info.param.name;
    });

TEST_F(MediaPipeCShimTest, DetectTakesTheLargestFrameAnIntCanSize) {
  FpFaceModel* model = Created();
  EXPECT_CALL(mock_, CreateImage(_, kIntMax / 4, 1, _, kIntMax / 4 * 4, _, _))
      .WillOnce(Return(kMpInvalidArgument));

  EXPECT_THAT(fp_face_model_detect(model, rgba_.data(), kIntMax / 4, 1, 0,
                                   &face_, error_.data(), error_.size()),
              Eq(-1));
  Destroy(model);
}

TEST_F(MediaPipeCShimTest, DestroyClosesTheLandmarker) {
  FpFaceModel* model = Created();

  Destroy(model);
}

TEST_F(MediaPipeCShimTest, DestroyFreesTheReasonAClosingFailureGives) {
  FpFaceModel* model = Created();
  char reason[] = "already closed";
  EXPECT_CALL(mock_, Close(fake_landmarker, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>(reason), Return(kMpInternal)));
  EXPECT_CALL(mock_, FreeError(reason));

  fp_face_model_destroy(model);
}

TEST_F(MediaPipeCShimTest, DestroyingNoModelDoesNothing) {
  fp_face_model_destroy(nullptr);
}

}  // namespace
}  // namespace facepalm

// The link-time seam: MediaPipe's C API, forwarding to the mock.
MpStatus MpFaceLandmarkerCreate(FaceLandmarkerOptions* options,
                                MpFaceLandmarkerPtr* landmarker,
                                char** error_msg) {
  return facepalm::media_pipe->Create(options, landmarker, error_msg);
}

MpStatus MpImageCreateFromUint8Data(MpImageFormat format, int width, int height,
                                    const uint8_t* pixel_data,
                                    int pixel_data_size, MpImagePtr* out,
                                    char** error_msg) {
  return facepalm::media_pipe->CreateImage(format, width, height, pixel_data,
                                           pixel_data_size, out, error_msg);
}

MpStatus MpFaceLandmarkerDetectForVideo(MpFaceLandmarkerPtr landmarker,
                                        MpImagePtr image,
                                        const ImageProcessingOptions* options,
                                        int64_t timestamp_ms,
                                        FaceLandmarkerResult* result,
                                        char** error_msg) {
  return facepalm::media_pipe->DetectForVideo(landmarker, image, options,
                                              timestamp_ms, result, error_msg);
}

void MpImageFree(MpImagePtr image) { facepalm::media_pipe->FreeImage(image); }

void MpFaceLandmarkerCloseResult(FaceLandmarkerResult* result) {
  facepalm::media_pipe->CloseResult(result);
}

MpStatus MpFaceLandmarkerClose(MpFaceLandmarkerPtr landmarker,
                               char** error_msg) {
  return facepalm::media_pipe->Close(landmarker, error_msg);
}

void MpErrorFree(char* error_message) {
  facepalm::media_pipe->FreeError(error_message);
}
