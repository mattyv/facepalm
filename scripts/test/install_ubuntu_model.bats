#!/usr/bin/env bats

setup() {
  root="${BATS_TEST_TMPDIR}/root"
  fake="${BATS_TEST_TMPDIR}/bin"
  mkdir -p "${fake}" "${root}/share/facepalm/model"
  export PATH="${fake}:${PATH}" TEST_ROOT="${BATS_TEST_TMPDIR}"
  export TMPDIR="${BATS_TEST_TMPDIR}"
  script="${BATS_TEST_DIRNAME}/../install_ubuntu_model.sh"
  cat >"${fake}/curl" <<'FAKE'
#!/bin/bash
printf '%s\n' "$*" > "${TEST_ROOT}/curl-args"
if [[ "${FAIL_CURL:-}" == 1 ]]; then
  while [[ "$1" != --output ]]; do shift; done
  printf partial > "$2"
  exit 1
fi
while [[ "$1" != --output ]]; do shift; done
printf 'model' > "$2"
FAKE
  cat >"${fake}/sha256sum" <<'FAKE'
#!/bin/bash
cat > "${TEST_ROOT}/hash-args"
[[ "${FAIL_HASH:-}" != 1 ]]
FAKE
  cat >"${fake}/install" <<'FAKE'
#!/bin/bash
[[ "${FAIL_INSTALL:-}" != 1 || "$1" == -d ]] || exit 1
exec /usr/bin/install "$@"
FAKE
  cat >"${fake}/mv" <<'FAKE'
#!/bin/bash
[[ "${FAIL_MOVE:-}" != 1 ]] || exit 1
exec /usr/bin/mv "$@"
FAKE
  chmod +x "${fake}/mv" "${fake}/curl" "${fake}/sha256sum" "${fake}/install"
  printf old > "${root}/share/facepalm/model/face_landmarker.task"
}

@test "installs verified model and uses the pinned URL" {
  run bash "${script}" --prefix "${root}"
  [ "$status" -eq 0 ]
  [ "$(cat "${root}/share/facepalm/model/face_landmarker.task")" = model ]
  grep -F 'generation=1782184688504388' "${TEST_ROOT}/curl-args"
  grep -F 'b261925d4aad812b47a0e8d58c1baa1223270a5d1f663d78338bc881c003879d' \
    "${TEST_ROOT}/hash-args"
  [ "$(find "${root}" -name '.facepalm-model.*' | wc -l)" -eq 0 ]
}

@test "download failure preserves the existing model" {
  export FAIL_CURL=1
  run bash "${script}" --prefix "${root}"
  [ "$status" -ne 0 ]
  [ "$(cat "${root}/share/facepalm/model/face_landmarker.task")" = old ]
  [ "$(find "${root}" -name '.facepalm-model.*' | wc -l)" -eq 0 ]
}

@test "bad hash preserves the existing model" {
  export FAIL_HASH=1
  run bash "${script}" --prefix "${root}"
  [ "$status" -ne 0 ]
  [ "$(cat "${root}/share/facepalm/model/face_landmarker.task")" = old ]
}

@test "install failure preserves the existing model" {
  export FAIL_INSTALL=1
  run bash "${script}" --prefix "${root}"
  [ "$status" -ne 0 ]
  [ "$(cat "${root}/share/facepalm/model/face_landmarker.task")" = old ]
  [ "$(find "${root}" -name '.facepalm-model.*' | wc -l)" -eq 0 ]
}

@test "invalid arguments fail without downloading" {
  run bash "${script}" --prefix
  [ "$status" -ne 0 ]
  [ ! -e "${TEST_ROOT}/curl-args" ]
  run bash "${script}" --unknown
  [ "$status" -ne 0 ]
  [ ! -e "${TEST_ROOT}/curl-args" ]
}

@test "rename failure preserves the old model and cleans up" {
  export FAIL_MOVE=1
  run bash "${script}" --prefix "${root}"
  [ "$status" -ne 0 ]
  [ "$(cat "${root}/share/facepalm/model/face_landmarker.task")" = old ]
  [ "$(find "${root}" -name '.facepalm-model.*' | wc -l)" -eq 0 ]
}

@test "model directory remains accessible with a restrictive umask" {
  rm -rf "${root}/share/facepalm/model"
  umask 077
  run bash "${script}" --prefix "${root}"
  [ "$status" -eq 0 ]
  [ "$(stat -c %a "${root}/share/facepalm/model")" = 755 ]
}

@test "a directory at the model path is rejected" {
  local destination="${root}/share/facepalm/model/face_landmarker.task"
  rm "${destination}"
  mkdir "${destination}"
  run bash "${script}" --prefix "${root}"
  [ "$status" -ne 0 ]
  [ -d "${destination}" ]
  [ -z "$(ls -A "${destination}")" ]
}
