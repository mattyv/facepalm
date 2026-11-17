#!/usr/bin/env bats
#
# Tests tools/fixtures/check_portrait.sh against a fake fixture tool and small
# golden files in MediaPipe's formats.

setup() {
  WORK="$(mktemp -d)"
  local mediapipe="${WORK}/repo/build-mediapipe/mediapipe"
  BIN="${mediapipe}/bazel-bin/facepalm_fixtures"
  GOLDEN="${mediapipe}/mediapipe/tasks/testdata/vision"
  mkdir -p "${WORK}/repo/tools/fixtures" "${BIN}" "${GOLDEN}" \
    "${WORK}/repo/build-mediapipe/data"
  cp "${BATS_TEST_DIRNAME}/../check_portrait.sh" "${WORK}/repo/tools/fixtures/"
  printf 'landmark {\n  x: 0.5\n  y: 0.25\n}\n' \
    >"${GOLDEN}/portrait_expected_face_landmarks.pbtxt"
  printf 'classification {\n  index: 0\n  score: 0.5\n  label: "a"\n}\n' \
    >"${GOLDEN}/portrait_expected_blendshapes.pbtxt"
  printf 'pose_transform_matrix {\n  packed_data: 1\n  packed_data: 0\n}\n' \
    >"${GOLDEN}/portrait_expected_face_geometry.pbtxt"
  result '[0.5, 0.25, -0.1]' 0.5 '1, 0'
}

teardown() {
  rm -rf "${WORK}"
}

# Makes the fake tool print one face: landmarks, one blendshape's score, and
# the pose values.
result() {
  cat >"${BIN}/face_landmarker_fixtures" <<EOF
#!/bin/bash
echo '[{"image": "portrait.jpg", "landmarks": [$1],
  "blendshapes": {"a": $2}, "pose_column_major": [$3]}]'
EOF
  chmod +x "${BIN}/face_landmarker_fixtures"
}

@test "passes when every value is within MediaPipe's margins" {
  result '[0.52, 0.23, 0]' 0.61 '1.03, -0.03'
  run bash "${WORK}/repo/tools/fixtures/check_portrait.sh"
  [[ "${status}" -eq 0 ]] || false
}

@test "fails, naming the output, when a landmark is beyond 0.03" {
  result '[0.54, 0.25, 0]' 0.5 '1, 0'
  run bash "${WORK}/repo/tools/fixtures/check_portrait.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'landmarks: 1 values outside 0.03'* ]] || false
}

@test "fails when a blendshape is beyond 0.12" {
  result '[0.5, 0.25, 0]' 0.63 '1, 0'
  run bash "${WORK}/repo/tools/fixtures/check_portrait.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'blendshapes: 1 values outside 0.12'* ]] || false
}

@test "fails when a matrix value is beyond 0.04" {
  result '[0.5, 0.25, 0]' 0.5 '1, 0.05'
  run bash "${WORK}/repo/tools/fixtures/check_portrait.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'pose: 1 values outside 0.04'* ]] || false
}

@test "counts a missing value as outside" {
  result '[0.5, 0.25, 0]' 0.5 '1'
  run bash "${WORK}/repo/tools/fixtures/check_portrait.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'pose: 1 values outside 0.04'* ]] || false
}

@test "fails when the tool fails" {
  printf '#!/bin/bash\nexit 1\n' >"${BIN}/face_landmarker_fixtures"
  run bash "${WORK}/repo/tools/fixtures/check_portrait.sh"
  [[ "${status}" -ne 0 ]] || false
}
