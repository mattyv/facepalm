#!/usr/bin/env bats
#
# Tests tools/fixtures/test_shim.sh against fake Bazel, test binary and LLVM
# tools on the PATH.

setup() {
  WORK="$(mktemp -d)"
  local mediapipe="${WORK}/repo/build-mediapipe/mediapipe"
  mkdir -p "${WORK}/repo/tools/fixtures" "${WORK}/bin" \
    "${mediapipe}/bazel-bin/facepalm_shim"
  cp "${BATS_TEST_DIRNAME}/../test_shim.sh" "${WORK}/repo/tools/fixtures/"
  export LOG="${WORK}/log" FAKE_TEST_STATUS=0
  export FAKE_COVER='100.00% 100.00% 100.00% 100.00%'
  cat >"${WORK}/bin/bazelisk" <<'EOF'
#!/bin/bash
echo "bazelisk $*" >>"${LOG}"
EOF
  # The test binary: records where it was told to find data, and passes or
  # fails as FAKE_TEST_STATUS says.
  cat >"${mediapipe}/bazel-bin/facepalm_shim/facepalm_mediapipe_test" <<'EOF'
#!/bin/bash
echo "test FACEPALM_DATA=${FACEPALM_DATA}" >>"${LOG}"
exit "${FAKE_TEST_STATUS}"
EOF
  # xcrun llvm-cov report prints one row for the shim's source, with each
  # measure's cover from FAKE_COVER.
  cat >"${WORK}/bin/xcrun" <<'EOF'
#!/bin/bash
if [[ "$1" == llvm-cov ]]; then
  read -r regions functions lines branches <<<"${FAKE_COVER}"
  echo "Filename Regions Missed Cover Functions Missed Executed Lines ..."
  echo "facepalm_shim/facepalm_mediapipe.cc 40 0 ${regions} 5 0" \
    "${functions} 62 0 ${lines} 26 0 ${branches}"
fi
EOF
  chmod +x "${WORK}"/bin/* "${mediapipe}"/bazel-bin/facepalm_shim/*
  PATH="${WORK}/bin:${PATH}"
  unset DEVELOPER_DIR BAZEL_DISK_CACHE
}

teardown() {
  rm -rf "${WORK}"
}

@test "passes when the tests pass and cover all of the shim" {
  run bash "${WORK}/repo/tools/fixtures/test_shim.sh"
  [[ "${status}" -eq 0 ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" == *'-fprofile-instr-generate,-fcoverage-mapping'* ]] || false
  [[ "${log}" == *'//facepalm_shim:facepalm_mediapipe_test'* ]] || false
  [[ "${log}" == *'FACEPALM_DATA='*'/build-mediapipe/data'* ]] || false
  [[ "${log}" != *DEVELOPER_DIR* ]] || false
}

@test "fails, naming the measure, below 100 percent" {
  FAKE_COVER='100.00% 100.00% 100.00% 96.15%'
  run bash "${WORK}/repo/tools/fixtures/test_shim.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'facepalm_mediapipe.cc: branches 96.15%'* ]] || false
}

@test "counts a measure with nothing to count as full" {
  FAKE_COVER='100.00% 100.00% 100.00% -'
  run bash "${WORK}/repo/tools/fixtures/test_shim.sh"
  [[ "${status}" -eq 0 ]] || false
}

@test "fails when the tests fail" {
  FAKE_TEST_STATUS=1
  run bash "${WORK}/repo/tools/fixtures/test_shim.sh"
  [[ "${status}" -ne 0 ]] || false
}

@test "passes DEVELOPER_DIR through to Bazel when it is set" {
  DEVELOPER_DIR=/Library/Developer/CommandLineTools \
    run bash "${WORK}/repo/tools/fixtures/test_shim.sh"
  [[ "${status}" -eq 0 ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" == *'--repo_env=DEVELOPER_DIR=/Library/Developer/'* ]] || false
}

@test "gives Bazel a disk cache when BAZEL_DISK_CACHE names one" {
  BAZEL_DISK_CACHE=/tmp/bazel-disk \
    run bash "${WORK}/repo/tools/fixtures/test_shim.sh"
  [[ "${status}" -eq 0 ]] || false
  log="$(cat "${LOG}")"
  [[ "${log}" == *'--disk_cache=/tmp/bazel-disk'* ]] || false
}
