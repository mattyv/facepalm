#!/usr/bin/env bats
#
# Tests tools/fixtures/build.sh against fake git, curl, shasum and Bazel on
# the PATH.

readonly PINNED="f8ef212d5c962c0e853db7e59d217056b187084b"

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/repo/tools/fixtures" "${WORK}/bin"
  cp "${BATS_TEST_DIRNAME}/../build.sh" "${WORK}/repo/tools/fixtures/"
  export LOG="${WORK}/log" FAKE_HEAD="${PINNED}" FAKE_SHASUM=0
  export FAKE_UNAME=Darwin
  # Logs each call; a clone makes its directory, and rev-parse prints HEAD.
  cat >"${WORK}/bin/uname" <<'EOF'
#!/bin/bash
printf '%s\n' "${FAKE_UNAME}"
EOF
  cat >"${WORK}/bin/git" <<'EOF'
#!/bin/bash
echo "git $*" >>"${LOG}"
if [[ "$1" == clone ]]; then mkdir -p "${@: -1}"; fi
if [[ "$*" == *rev-parse* ]]; then echo "${FAKE_HEAD}"; fi
EOF
  cat >"${WORK}/bin/curl" <<'EOF'
#!/bin/bash
echo "curl ${*: -1}" >>"${LOG}"
EOF
  cat >"${WORK}/bin/shasum" <<'EOF'
#!/bin/bash
exit "${FAKE_SHASUM}"
EOF
  cat >"${WORK}/bin/bazelisk" <<'EOF'
#!/bin/bash
echo "bazelisk $*" >>"${LOG}"
EOF
  chmod +x "${WORK}"/bin/*
  PATH="${WORK}/bin:${PATH}"
  unset DEVELOPER_DIR BAZEL_DISK_CACHE
}

teardown() {
  rm -rf "${WORK}"
}

@test "clones MediaPipe at the release, patches it, and builds both targets" {
  run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -eq 0 ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" == *'git clone --quiet --depth 1 --branch v0.10.35 '* ]] || false
  [[ "${log}" == *'apply '*'/tools/fixtures/opencv.diff'* ]] || false
  [[ "${log}" == *'face_landmarker_v2_with_blendshapes.task?generation='* ]] \
    || false
  [[ "${log}" == *'portrait.jpg?generation='* ]] || false
  [[ "${log}" == *'face_detection_short_range.tflite?generation='* ]] \
    || false
  [[ "${log}" == *'build '*'//facepalm_fixtures:face_landmarker_fixtures'* ]] \
    || false
  [[ "${log}" == *'//facepalm_shim:libfacepalm_mediapipe.dylib'* ]] || false
  [[ "${log}" != *disk_cache* ]] || false
  [[ "${log}" != *DEVELOPER_DIR* ]] || false
  [[ -L "${WORK}/repo/build-mediapipe/mediapipe/facepalm_fixtures" ]] || false
  [[ -L "${WORK}/repo/build-mediapipe/mediapipe/facepalm_shim" ]] || false
  [[ -L "${WORK}/repo/build-mediapipe/mediapipe/src" ]] || false
}

@test "leaves an existing checkout as it is" {
  mkdir -p "${WORK}/repo/build-mediapipe/mediapipe"
  run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -eq 0 ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" != *clone* ]] || false
}

@test "fails when the checkout is not at the pinned commit" {
  FAKE_HEAD=0000000000000000000000000000000000000000
  run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *"MediaPipe is at ${FAKE_HEAD}, not ${PINNED}"* ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" != *bazelisk* ]] || false
}

@test "fails when a download does not match its checksum" {
  FAKE_SHASUM=1
  run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -ne 0 ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" != *bazelisk* ]] || false
}

@test "passes DEVELOPER_DIR through to Bazel when it is set" {
  DEVELOPER_DIR=/Library/Developer/CommandLineTools \
    run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -eq 0 ]] || false
  log="$(cat "${LOG}")"
  for flag in repo_env action_env host_action_env; do
    [[ "${log}" == *"--${flag}=DEVELOPER_DIR=/Library/Developer/"* ]] || false
  done
}

@test "gives Bazel a disk cache when BAZEL_DISK_CACHE names one" {
  BAZEL_DISK_CACHE=/tmp/bazel-disk \
    run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -eq 0 ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" == *'--disk_cache=/tmp/bazel-disk'* ]] || false
}

# The Linux daemon links MediaPipe's own libmediapipe.so (#60, #65), so
# there is no shim for Bazel to build there.
@test "refuses Linux before fetching or building" {
  FAKE_UNAME=Linux run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -ne 0 ]] || false
  [[ "${output}" == *'libmediapipe.so'* ]] || false
  [[ ! -e "${LOG}" ]] || false
}

@test "rejects unsupported systems before fetching or building" {
  FAKE_UNAME=FreeBSD run bash "${WORK}/repo/tools/fixtures/build.sh"
  [[ "${status}" -ne 0 ]] || false
  [[ "${output}" == *'unsupported operating system'* ]] || false
  [[ ! -e "${LOG}" ]] || false
}
