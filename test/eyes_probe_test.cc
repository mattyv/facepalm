#include "src/eyes_probe.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "src/face_model.h"

namespace facepalm {
namespace {

// A face with a distinct value on every score and landmark FormatEyesLine
// reads, so a wrong index or a wrong order shows up as a wrong number in
// the wrong place.
Face DistinctFace() {
  Face face{};
  face.blendshapes[11] = 0.11F;  // eyeLookDownLeft
  face.blendshapes[12] = 0.12F;  // eyeLookDownRight
  face.blendshapes[13] = 0.13F;  // eyeLookInLeft
  face.blendshapes[14] = 0.14F;  // eyeLookInRight
  face.blendshapes[15] = 0.15F;  // eyeLookOutLeft
  face.blendshapes[16] = 0.16F;  // eyeLookOutRight
  face.blendshapes[17] = 0.17F;  // eyeLookUpLeft
  face.blendshapes[18] = 0.18F;  // eyeLookUpRight
  face.blendshapes[9] = 0.09F;   // eyeBlinkLeft
  face.blendshapes[10] = 0.10F;  // eyeBlinkRight
  face.blendshapes[19] = 0.19F;  // eyeSquintLeft
  face.blendshapes[20] = 0.20F;  // eyeSquintRight
  face.blendshapes[21] = 0.21F;  // eyeWideLeft
  face.blendshapes[22] = 0.22F;  // eyeWideRight
  face.landmarks[468][0] = 0.468F;
  face.landmarks[468][1] = 0.1F;
  face.landmarks[468][2] = 0.01F;
  face.landmarks[473][0] = 0.473F;
  face.landmarks[473][1] = 0.2F;
  face.landmarks[473][2] = 0.02F;
  face.landmarks[33][0] = 0.033F;
  face.landmarks[33][1] = 0.3F;
  face.landmarks[33][2] = 0.03F;
  face.landmarks[133][0] = 0.133F;
  face.landmarks[133][1] = 0.4F;
  face.landmarks[133][2] = 0.04F;
  face.landmarks[362][0] = 0.362F;
  face.landmarks[362][1] = 0.5F;
  face.landmarks[362][2] = 0.05F;
  face.landmarks[263][0] = 0.263F;
  face.landmarks[263][1] = 0.6F;
  face.landmarks[263][2] = 0.06F;
  return face;
}

TEST(EyesProbeTest, FormatsEveryScoreAndLandmarkInOrder) {
  EXPECT_EQ(FormatEyesLine(DistinctFace(), 1234),
            "eyes 1234"
            " 0.110 0.120 0.130 0.140 0.150 0.160 0.170 0.180"
            " 0.090 0.100 0.190 0.200 0.210 0.220"
            " 0.468000 0.100000 0.010000"
            " 0.473000 0.200000 0.020000"
            " 0.033000 0.300000 0.030000"
            " 0.133000 0.400000 0.040000"
            " 0.362000 0.500000 0.050000"
            " 0.263000 0.600000 0.060000");
}

TEST(EyesProbeTest, FormatsAllZerosAndAWideTimestamp) {
  const Face face{};
  EXPECT_EQ(FormatEyesLine(face, 9876543210),
            "eyes 9876543210"
            " 0.000 0.000 0.000 0.000 0.000 0.000 0.000 0.000"
            " 0.000 0.000 0.000 0.000 0.000 0.000"
            " 0.000000 0.000000 0.000000"
            " 0.000000 0.000000 0.000000"
            " 0.000000 0.000000 0.000000"
            " 0.000000 0.000000 0.000000"
            " 0.000000 0.000000 0.000000"
            " 0.000000 0.000000 0.000000");
}

}  // namespace
}  // namespace facepalm
