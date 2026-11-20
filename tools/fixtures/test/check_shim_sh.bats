#!/usr/bin/env bats
#
# Tests tools/fixtures/check_shim.sh against fake nm and otool on the PATH.

setup() {
  WORK="$(mktemp -d)"
  SHIM="${WORK}/repo/build-mediapipe/mediapipe/bazel-bin/facepalm_shim"
  mkdir -p "${WORK}/repo/tools/fixtures" "${WORK}/bin" "${SHIM}"
  cp "${BATS_TEST_DIRNAME}/../check_shim.sh" "${WORK}/repo/tools/fixtures/"
  printf 'x' >"${SHIM}/libfacepalm_mediapipe.dylib"
  # Prints FAKE_NM for nm -gU, and FAKE_OTOOL for otool -L.
  cat >"${WORK}/bin/nm" <<'EOF'
#!/bin/bash
printf "%b" "${FAKE_NM}"
EOF
  cat >"${WORK}/bin/otool" <<'EOF'
#!/bin/bash
printf "%b" "${FAKE_OTOOL}"
EOF
  chmod +x "${WORK}"/bin/*
  PATH="${WORK}/bin:${PATH}"
  export FAKE_NM='0 T _fp_face_model_create\n0 T _fp_face_model_destroy\n'
  FAKE_NM+='0 T _fp_face_model_detect\n0 T _fp_face_detector_create\n'
  FAKE_NM+='0 T _fp_face_detector_destroy\n0 T _fp_face_detector_detect\n'
  export FAKE_OTOOL='lib:\n\t@rpath/libfacepalm_mediapipe.dylib\n'
  FAKE_OTOOL+='\t/usr/lib/libc++.1.dylib\n'
}

teardown() {
  rm -rf "${WORK}"
}

@test "passes, with the size, when only the interface is exported" {
  run bash "${WORK}/repo/tools/fixtures/check_shim.sh"
  [[ "${status}" -eq 0 ]] || false
  [[ "${output}" == *'libfacepalm_mediapipe.dylib: 1 bytes'* ]] || false
}

@test "fails, naming it, when another symbol is exported" {
  FAKE_NM+='0 T __ZN4absl6StatusD1Ev\n'
  run bash "${WORK}/repo/tools/fixtures/check_shim.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'exports __ZN4absl6StatusD1Ev'* ]] || false
}

@test "fails when an interface function is missing" {
  FAKE_NM='0 T _fp_face_model_create\n0 T _fp_face_model_destroy\n'
  run bash "${WORK}/repo/tools/fixtures/check_shim.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'does not export _fp_face_model_detect'* ]] || false
  [[ "${output}" == *'does not export _fp_face_detector_detect'* ]] || false
}

@test "fails when it links an OpenCV dylib" {
  FAKE_OTOOL+='\t@rpath/libopencv_core.3.4.dylib\n'
  run bash "${WORK}/repo/tools/fixtures/check_shim.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'links @rpath/libopencv_core.3.4.dylib'* ]] || false
}

@test "fails when the dylib was not built" {
  rm "${SHIM}/libfacepalm_mediapipe.dylib"
  run bash "${WORK}/repo/tools/fixtures/check_shim.sh"
  [[ "${status}" -ne 0 ]] || false
}
