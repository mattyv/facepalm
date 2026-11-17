#!/usr/bin/env bats
#
# Tests tools/fixtures/check_face_model.sh against a fake sips on the PATH, a
# fake fixture tool, and a fake face_model_check.

setup() {
  WORK="$(mktemp -d)"
  FIXTURES="${WORK}/repo/build-mediapipe/mediapipe/bazel-bin/facepalm_fixtures"
  mkdir -p "${WORK}/repo/tools/fixtures" "${WORK}/bin" "${FIXTURES}" \
    "${WORK}/repo/build-mediapipe/data"
  cp "${BATS_TEST_DIRNAME}/../check_face_model.sh" \
    "${WORK}/repo/tools/fixtures/"
  # Records its arguments, as converting the portrait would.
  cat >"${WORK}/bin/sips" <<EOF
#!/bin/bash
echo "\$@" >"${WORK}/sips.args"
EOF
  chmod +x "${WORK}/bin/sips"
  PATH="${WORK}/bin:${PATH}"
  face "${FIXTURES}/face_landmarker_fixtures" 0.5 0.25 '1, 0'
  face "${WORK}/face_model_check" 0.5 0.25 '1, 0'
}

teardown() {
  rm -rf "${WORK}"
}

# Writes a fake tool at $1 that records its arguments and prints one face
# with landmark x $2, blendshape $3 and pose values $4.
face() {
  cat >"$1" <<EOF
#!/bin/bash
echo "\$@" >"$1.args"
echo '[{"image": "\$3", "landmarks": [[$2, 0.1, 0]],
  "blendshapes": {"name": $3}, "pose_column_major": [$4]}]'
EOF
  chmod +x "$1"
}

@test "passes, counting the values, when both tools match exactly" {
  run bash "${WORK}/repo/tools/fixtures/check_face_model.sh" \
    "${WORK}/face_model_check"
  [[ "${status}" -eq 0 ]] || false
  [[ "${output}" == *'matches face_landmarker_fixtures: 6 values'* ]] || false
}

@test "runs both tools on a PNG of the portrait, the fixture tool in VIDEO" {
  run bash "${WORK}/repo/tools/fixtures/check_face_model.sh" \
    "${WORK}/face_model_check"
  local sips fixtures check
  sips="$(<"${WORK}/sips.args")"
  fixtures="$(<"${FIXTURES}/face_landmarker_fixtures.args")"
  check="$(<"${WORK}/face_model_check.args")"
  [[ "${sips}" == '-s format png '*'/portrait.jpg --out '*'/portrait.png' ]] \
    || false
  [[ "${fixtures}" == '--video '*'.task '*'/portrait.png' ]] || false
  [[ "${check}" == *'.task '*'/portrait.png' ]] || false
}

@test "fails, showing the difference, when a landmark differs at all" {
  face "${WORK}/face_model_check" 0.5000001 0.25 '1, 0'
  run bash "${WORK}/repo/tools/fixtures/check_face_model.sh" \
    "${WORK}/face_model_check"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'differs from face_landmarker_fixtures'* ]] || false
  [[ "${output}" == *'> 0.5000001'* ]] || false
}

@test "fails when a blendshape differs" {
  face "${WORK}/face_model_check" 0.5 0.26 '1, 0'
  run bash "${WORK}/repo/tools/fixtures/check_face_model.sh" \
    "${WORK}/face_model_check"
  [[ "${status}" -eq 1 ]] || false
}

@test "fails when a pose value is missing" {
  face "${WORK}/face_model_check" 0.5 0.25 '1'
  run bash "${WORK}/repo/tools/fixtures/check_face_model.sh" \
    "${WORK}/face_model_check"
  [[ "${status}" -eq 1 ]] || false
}

@test "fails when face_model_check fails" {
  printf '#!/bin/bash\nexit 1\n' >"${WORK}/face_model_check"
  run bash "${WORK}/repo/tools/fixtures/check_face_model.sh" \
    "${WORK}/face_model_check"
  [[ "${status}" -ne 0 ]] || false
}

@test "fails, with usage, without face_model_check" {
  run bash "${WORK}/repo/tools/fixtures/check_face_model.sh"
  [[ "${status}" -eq 2 ]] || false
  [[ "${output}" == *'usage: check_face_model.sh'* ]] || false
}
