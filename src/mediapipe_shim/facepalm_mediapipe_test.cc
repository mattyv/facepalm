// Tests facepalm_mediapipe.h against the real face landmarker. It runs inside
// MediaPipe's Bazel workspace (tools/fixtures/check_shim.sh), with
// FACEPALM_DATA naming the folder tools/fixtures/build.sh downloads MediaPipe's
// test model and portrait into.

#include "facepalm_shim/facepalm_mediapipe.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "facepalm_shim/facepalm_mediapipe_internal.h"
#include "mediapipe/framework/formats/image.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/framework/formats/matrix.h"
#include "mediapipe/framework/port/gtest.h"
#include "mediapipe/tasks/cc/components/containers/category.h"
#include "mediapipe/tasks/cc/components/containers/classification_result.h"
#include "mediapipe/tasks/cc/components/containers/detection_result.h"
#include "mediapipe/tasks/cc/components/containers/keypoint.h"
#include "mediapipe/tasks/cc/components/containers/landmark.h"
#include "mediapipe/tasks/cc/components/containers/rect.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker_result.h"
#include "mediapipe/tasks/cc/vision/utils/image_utils.h"

namespace {

constexpr size_t kErrorSize = 256;

// MediaPipe's expected transformation matrix for portrait.jpg, from
// mediapipe/tasks/testdata/vision/portrait_expected_face_geometry.pbtxt, and
// the margins MediaPipe's face_landmarker_test.cc allows.
constexpr std::array<float, FP_POSE_VALUES> kPortraitPose = {
    0.9995292F,   0.01715696F, -0.03715533F, 0,  //
    -0.01294756F, 0.9937692F,  0.11070588F,  0,  //
    0.038823195F, -0.1101321F, 0.99315894F,  0,  //
    -0.3691378F,  22.75809F,   -65.765925F,  1};
constexpr float kPoseMargin = 0.04F;
// Landmark 0, from portrait_expected_face_landmarks.pbtxt.
constexpr float kPortraitLandmarkX = 0.49662378F;
constexpr float kPortraitLandmarkY = 0.25066471F;
constexpr float kLandmarkMargin = 0.03F;

std::string Data(const std::string& name) {
  const char* data = std::getenv("FACEPALM_DATA");
  return std::string(data == nullptr ? "" : data) + "/" + name;
}

// A frame of packed RGBA, as facepalm hands the shim.
struct Rgba {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;
};

// Decodes `path` and repacks it as RGBA with opaque alpha.
Rgba Decode(const std::string& path) {
  absl::StatusOr<mediapipe::Image> image =
      mediapipe::tasks::vision::DecodeImageFromFile(path);
  EXPECT_TRUE(image.ok()) << image.status();
  if (!image.ok()) return {};
  const mediapipe::ImageFrame& frame = *image->GetImageFrameSharedPtr();
  Rgba rgba{.width = frame.Width(), .height = frame.Height()};
  rgba.pixels.resize(static_cast<size_t>(rgba.width) * rgba.height * 4);
  const int channels = frame.NumberOfChannels();
  for (int y = 0; y < rgba.height; ++y) {
    const uint8_t* row = frame.PixelData() + (y * frame.WidthStep());
    for (int x = 0; x < rgba.width; ++x) {
      uint8_t* out =
          &rgba.pixels[(static_cast<size_t>(y) * rgba.width + x) * 4];
      out[0] = row[x * channels];
      out[1] = row[x * channels + 1];
      out[2] = row[x * channels + 2];
      out[3] = 255;
    }
  }
  return rgba;
}

class FacepalmMediapipeTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(fp_face_model_create(
                  Data("face_landmarker_v2_with_blendshapes.task").c_str(),
                  &model_, error_, kErrorSize),
              0)
        << error_;
  }
  void TearDown() override { fp_face_model_destroy(model_); }

  FpFaceModel* model_ = nullptr;
  char error_[kErrorSize] = {};
  FpFace face_ = {};
};

TEST_F(FacepalmMediapipeTest, FindsThePortraitsFaceAsMediaPipeExpects) {
  const Rgba portrait = Decode(Data("portrait.jpg"));
  ASSERT_EQ(fp_face_model_detect(model_, portrait.pixels.data(), portrait.width,
                                 portrait.height, /*timestamp_ms=*/0, &face_,
                                 error_, kErrorSize),
            1)
      << error_;
  for (int i = 0; i < FP_POSE_VALUES; ++i) {
    EXPECT_NEAR(face_.pose_column_major[i], kPortraitPose[i], kPoseMargin)
        << "pose value " << i;
  }
  EXPECT_NEAR(face_.landmarks[0][0], kPortraitLandmarkX, kLandmarkMargin);
  EXPECT_NEAR(face_.landmarks[0][1], kPortraitLandmarkY, kLandmarkMargin);
  float total = 0;
  for (float score : face_.blendshapes) {
    EXPECT_GE(score, 0);
    EXPECT_LE(score, 1);
    total += score;
  }
  EXPECT_GT(total, 0);
}

TEST_F(FacepalmMediapipeTest, FindsNoFaceInABlankFrame) {
  const std::vector<uint8_t> black(640 * 480 * 4, 0);
  EXPECT_EQ(fp_face_model_detect(model_, black.data(), 640, 480, 0, &face_,
                                 error_, kErrorSize),
            0)
      << error_;
}

TEST_F(FacepalmMediapipeTest, FailsOnTimestampsThatDoNotIncrease) {
  const std::vector<uint8_t> black(64 * 64 * 4, 0);
  ASSERT_GE(fp_face_model_detect(model_, black.data(), 64, 64, 10, &face_,
                                 error_, kErrorSize),
            0);
  EXPECT_EQ(fp_face_model_detect(model_, black.data(), 64, 64, 10, &face_,
                                 error_, kErrorSize),
            -1);
  EXPECT_NE(std::strlen(error_), 0U);
}

TEST_F(FacepalmMediapipeTest, FailsOnAFrameWithNoPixels) {
  const std::vector<uint8_t> none(4, 0);
  EXPECT_EQ(fp_face_model_detect(model_, none.data(), 0, 0, 0, &face_, error_,
                                 kErrorSize),
            -1);
  EXPECT_NE(std::strlen(error_), 0U);
}

TEST(FacepalmMediapipeCreateTest, FailsWithAMissingModelAndSaysWhy) {
  FpFaceModel* model = nullptr;
  char error[kErrorSize] = {};
  EXPECT_EQ(fp_face_model_create("/nonexistent/model.task", &model, error,
                                 kErrorSize),
            -1);
  EXPECT_EQ(model, nullptr);
  EXPECT_NE(std::string(error).find("model.task"), std::string::npos) << error;
}

TEST(FacepalmMediapipeCreateTest, CutsTheErrorToFitAndEndsIt) {
  FpFaceModel* model = nullptr;
  char error[8];
  std::memset(error, 'x', sizeof(error));
  EXPECT_EQ(fp_face_model_create("/nonexistent/model.task", &model, error,
                                 sizeof(error)),
            -1);
  EXPECT_EQ(std::strlen(error), sizeof(error) - 1);
}

TEST(FacepalmMediapipeCreateTest, ToleratesNoErrorBuffer) {
  FpFaceModel* model = nullptr;
  EXPECT_EQ(fp_face_model_create("/nonexistent/model.task", &model, nullptr, 0),
            -1);
}

TEST(FacepalmMediapipeDestroyTest, AcceptsNoModel) {
  fp_face_model_destroy(nullptr);
}

using ::mediapipe::tasks::components::containers::Category;
using ::mediapipe::tasks::components::containers::Classifications;
using ::mediapipe::tasks::components::containers::NormalizedLandmark;
using ::mediapipe::tasks::components::containers::NormalizedLandmarks;
using ::mediapipe::tasks::vision::face_landmarker::FaceLandmarkerResult;

// A made-up result with one face of the given shape: a real model's result
// always has 478 landmarks, 52 blendshapes and a 4x4 pose. Each value is
// distinct, so a copy from the wrong place shows.
FaceLandmarkerResult Result(int landmarks, int blendshapes, int pose_side) {
  FaceLandmarkerResult result;
  NormalizedLandmarks face;
  for (int i = 0; i < landmarks; ++i) {
    face.landmarks.push_back(
        NormalizedLandmark{.x = i * 0.001F, .y = i * 0.002F, .z = i * 0.003F});
  }
  result.face_landmarks.push_back(face);
  if (blendshapes >= 0) {
    Classifications scores;
    for (int i = 0; i < blendshapes; ++i) {
      scores.categories.push_back(Category{.index = i, .score = i * 0.01F});
    }
    result.face_blendshapes = std::vector<Classifications>{scores};
  }
  if (pose_side >= 0) {
    mediapipe::Matrix pose(pose_side, pose_side);
    for (int i = 0; i < pose.size(); ++i)
      pose.data()[i] = static_cast<float>(i);
    result.facial_transformation_matrixes =
        std::vector<mediapipe::Matrix>{pose};
  }
  return result;
}

TEST(CopyFaceTest, CopiesEachValueFromItsPlace) {
  FpFace face = {};
  char error[kErrorSize] = {};
  ASSERT_EQ(facepalm_shim::CopyFace(Result(FP_LANDMARKS, FP_BLENDSHAPES, 4),
                                    face, error, kErrorSize),
            1);
  EXPECT_FLOAT_EQ(face.landmarks[477][0], 477 * 0.001F);
  EXPECT_FLOAT_EQ(face.landmarks[477][1], 477 * 0.002F);
  EXPECT_FLOAT_EQ(face.landmarks[477][2], 477 * 0.003F);
  EXPECT_FLOAT_EQ(face.blendshapes[51], 51 * 0.01F);
  EXPECT_FLOAT_EQ(face.pose_column_major[13], 13);
}

TEST(CopyFaceTest, FailsOnTheWrongNumberOfLandmarks) {
  FpFace face = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyFace(Result(468, FP_BLENDSHAPES, 4), face, error,
                                    kErrorSize),
            -1);
  EXPECT_NE(std::string(error).find("shape"), std::string::npos) << error;
}

TEST(CopyFaceTest, FailsWithoutBlendshapes) {
  FpFace face = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyFace(Result(FP_LANDMARKS, -1, 4), face, error,
                                    kErrorSize),
            -1);
}

TEST(CopyFaceTest, FailsWithoutAPose) {
  FpFace face = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyFace(Result(FP_LANDMARKS, FP_BLENDSHAPES, -1),
                                    face, error, kErrorSize),
            -1);
}

TEST(CopyFaceTest, FailsOnTheWrongNumberOfBlendshapes) {
  FpFace face = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyFace(Result(FP_LANDMARKS, 51, 4), face, error,
                                    kErrorSize),
            -1);
  EXPECT_NE(std::string(error).find("blendshape"), std::string::npos) << error;
}

TEST(CopyFaceTest, FailsOnAPoseThatIsNotFourByFour) {
  FpFace face = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyFace(Result(FP_LANDMARKS, FP_BLENDSHAPES, 3),
                                    face, error, kErrorSize),
            -1);
  EXPECT_NE(std::string(error).find("pose"), std::string::npos) << error;
}

TEST(FailTest, LeavesAnEmptyErrorBufferAlone) {
  char error[1] = {'x'};
  EXPECT_EQ(facepalm_shim::Fail(absl::InternalError("why"), error, 0), -1);
  EXPECT_EQ(error[0], 'x');
}

// MediaPipe's expected short-range detection for portrait.jpg (820x1024),
// from mediapipe/tasks/testdata/vision/portrait_expected_detection.pbtxt,
// and the margins face_detector_test.cc allows: 5 pixels for the box, 0.01
// for key points.
constexpr int kPortraitWidth = 820;
constexpr int kPortraitHeight = 1024;
constexpr std::array<float, 4> kPortraitBoxPixels = {283, 115, 234, 234};
constexpr float kBoxMarginPixels = 5;
constexpr std::array<std::array<float, 2>, FP_KEYPOINTS> kPortraitKeypoints = {
    {{0.44416338F, 0.17643969F},
     {0.55514044F, 0.17731678F},
     {0.5046702F, 0.2265771F},
     {0.50227845F, 0.2719954F},
     {0.36063305F, 0.20143759F},
     {0.6084143F, 0.20409837F}}};
constexpr float kKeypointMargin = 0.01F;

class FaceDetectorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(fp_face_detector_create(
                  Data("face_detection_short_range.tflite").c_str(), &detector_,
                  error_, kErrorSize),
              0)
        << error_;
  }
  void TearDown() override { fp_face_detector_destroy(detector_); }

  FpFaceDetector* detector_ = nullptr;
  char error_[kErrorSize] = {};
  FpDetection detection_ = {};
};

TEST_F(FaceDetectorTest, FindsThePortraitsFaceAsMediaPipeExpects) {
  const Rgba portrait = Decode(Data("portrait.jpg"));
  ASSERT_EQ(portrait.width, kPortraitWidth);
  ASSERT_EQ(
      fp_face_detector_detect(detector_, portrait.pixels.data(), portrait.width,
                              portrait.height, &detection_, error_, kErrorSize),
      1)
      << error_;
  const std::array<float, 4> sides = {kPortraitWidth, kPortraitHeight,
                                      kPortraitWidth, kPortraitHeight};
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(detection_.box[i] * sides[i], kPortraitBoxPixels[i],
                kBoxMarginPixels)
        << "box value " << i;
  }
  for (int i = 0; i < FP_KEYPOINTS; ++i) {
    EXPECT_NEAR(detection_.keypoints[i][0], kPortraitKeypoints[i][0],
                kKeypointMargin)
        << "key point " << i;
    EXPECT_NEAR(detection_.keypoints[i][1], kPortraitKeypoints[i][1],
                kKeypointMargin)
        << "key point " << i;
  }
}

TEST_F(FaceDetectorTest, FindsNoFaceInABlankFrame) {
  const std::vector<uint8_t> black(640 * 480 * 4, 0);
  EXPECT_EQ(fp_face_detector_detect(detector_, black.data(), 640, 480,
                                    &detection_, error_, kErrorSize),
            0)
      << error_;
}

TEST_F(FaceDetectorTest, FailsOnAFrameWithNoPixels) {
  const std::vector<uint8_t> none(4, 0);
  EXPECT_EQ(fp_face_detector_detect(detector_, none.data(), 0, 0, &detection_,
                                    error_, kErrorSize),
            -1);
  EXPECT_NE(std::strlen(error_), 0U);
}

TEST(FaceDetectorCreateTest, FailsWithAMissingModelAndSaysWhy) {
  FpFaceDetector* detector = nullptr;
  char error[kErrorSize] = {};
  EXPECT_EQ(fp_face_detector_create("/nonexistent/detector.tflite", &detector,
                                    error, kErrorSize),
            -1);
  EXPECT_EQ(detector, nullptr);
  EXPECT_NE(std::string(error).find("detector.tflite"), std::string::npos)
      << error;
}

TEST(FaceDetectorDestroyTest, AcceptsNoDetector) {
  fp_face_detector_destroy(nullptr);
}

using ::mediapipe::tasks::components::containers::Detection;
using ::mediapipe::tasks::components::containers::DetectionResult;
using ::mediapipe::tasks::components::containers::NormalizedKeypoint;

// A made-up result of `faces` faces, the first with `keypoints` key points
// at distinct places and a box at (10, 20) to (110, 70) pixels.
DetectionResult Detections(int faces, int keypoints) {
  DetectionResult result;
  for (int f = 0; f < faces; ++f) {
    Detection detection;
    detection.bounding_box = {
        .left = 10, .top = 20, .right = 110, .bottom = 70};
    if (keypoints >= 0) {
      std::vector<NormalizedKeypoint> points;
      for (int i = 0; i < keypoints; ++i) {
        points.push_back(NormalizedKeypoint{.x = i * 0.1F, .y = i * 0.2F});
      }
      detection.keypoints = points;
    }
    result.detections.push_back(detection);
  }
  return result;
}

TEST(CopyDetectionTest, NormalisesTheBoxAndCopiesTheKeyPoints) {
  FpDetection detection = {};
  char error[kErrorSize] = {};
  ASSERT_EQ(facepalm_shim::CopyDetection(Detections(1, FP_KEYPOINTS), 200, 100,
                                         detection, error, kErrorSize),
            1);
  EXPECT_FLOAT_EQ(detection.box[0], 0.05F);
  EXPECT_FLOAT_EQ(detection.box[1], 0.2F);
  EXPECT_FLOAT_EQ(detection.box[2], 0.5F);
  EXPECT_FLOAT_EQ(detection.box[3], 0.5F);
  EXPECT_FLOAT_EQ(detection.keypoints[5][0], 0.5F);
  EXPECT_FLOAT_EQ(detection.keypoints[5][1], 1.0F);
}

TEST(CopyDetectionTest, CountsEveryFace) {
  FpDetection detection = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyDetection(Detections(2, FP_KEYPOINTS), 200, 100,
                                         detection, error, kErrorSize),
            2);
}

TEST(CopyDetectionTest, FailsWithoutKeyPoints) {
  FpDetection detection = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyDetection(Detections(1, -1), 200, 100, detection,
                                         error, kErrorSize),
            -1);
  EXPECT_NE(std::string(error).find("key points"), std::string::npos) << error;
}

TEST(CopyDetectionTest, FailsOnTheWrongNumberOfKeyPoints) {
  FpDetection detection = {};
  char error[kErrorSize] = {};
  EXPECT_EQ(facepalm_shim::CopyDetection(Detections(1, 5), 200, 100, detection,
                                         error, kErrorSize),
            -1);
}

}  // namespace
