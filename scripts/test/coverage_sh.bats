#!/usr/bin/env bats
#
# Tests scripts/coverage.sh against fake build and LLVM tools on the PATH.

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/repo/scripts" "${WORK}/repo/build-coverage" "${WORK}/bin"
  cp "${BATS_TEST_DIRNAME}/../coverage.sh" "${WORK}/repo/scripts/"
  touch "${WORK}/repo/build-coverage/head_pose_test"
  chmod +x "${WORK}/repo/build-coverage/head_pose_test"
  for tool in cmake ctest llvm-profdata; do
    printf '#!/bin/bash\n' >"${WORK}/bin/${tool}"
  done
  # Stands in for "xcrun --find <tool>", answering with the tool on the PATH,
  # and for uname, answering Linux unless a test says otherwise.
  cat >"${WORK}/bin/xcrun" <<'EOF'
#!/bin/bash
echo "$2" >>"${WORK}/xcrun.log"
echo "$2"
EOF
  cat >"${WORK}/bin/uname" <<'EOF'
#!/bin/bash
echo "${FAKE_UNAME:-Linux}"
EOF
  printf '#!/bin/bash\ncat "%s"\n' "${WORK}/summary.json" \
    >"${WORK}/bin/llvm-cov"
  chmod +x "${WORK}"/bin/*
  PATH="${WORK}/bin:${PATH}"
  export WORK
  floors '"src/head_pose.cc":
    {"unit": {"lines": 100, "branches": 100, "functions": 100}}'
}

teardown() {
  rm -rf "${WORK}"
}

# Writes coverage_thresholds.json with the given entries.
floors() {
  printf '{%s}\n' "$1" >"${WORK}/repo/coverage_thresholds.json"
}

# Writes what the fake llvm-cov reports, as "count percent" for each measure.
summary() {
  printf '{"data": [{"files": [{"summary": {
    "lines": {"count": %s, "percent": %s},
    "branches": {"count": %s, "percent": %s},
    "functions": {"count": %s, "percent": %s}}}]}]}\n' "$@" \
    >"${WORK}/summary.json"
}

@test "passes when every measure meets its floor" {
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 0 ]] || false
}

@test "fails, naming the file and measure, when one is below its floor" {
  summary 10 90 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'src/head_pose.cc: lines 90% is below 100%'* ]] || false
}

@test "counts a measure with nothing to count as full" {
  summary 10 100 0 0 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 0 ]] || false
}

@test "holds a unit with no floors listed to 100" {
  floors ''
  summary 10 100 4 75 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'branches 75% is below 100%'* ]] || false
}

@test "fails when a unit's profiles cannot be merged" {
  printf '#!/bin/bash\nexit 1\n' >"${WORK}/bin/llvm-profdata"
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -ne 0 ]] || false
}

@test "fails when a file has floors but no test executable" {
  rm "${WORK}/repo/build-coverage/head_pose_test"
  touch "${WORK}/repo/build-coverage/other_test"
  chmod +x "${WORK}/repo/build-coverage/other_test"
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'src/head_pose.cc: has floors but no test executable'* ]] \
    || false
}

@test "leaves a file whose floors are for another measure to that measure" {
  floors '"src/head_pose.cc":
    {"unit": {"lines": 100, "branches": 100, "functions": 100}},
    "scripts/coverage.sh": {"shell": {"lines": 100}}'
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 0 ]] || false
}

@test "finds the LLVM tools through xcrun on macOS" {
  summary 10 100 4 100 2 100
  FAKE_UNAME=Darwin run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 0 ]] || false
  [[ "$(<"${WORK}/xcrun.log")" == $'llvm-profdata\nllvm-cov' ]] || false
}

@test "runs the LLVM tools from the PATH elsewhere" {
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 0 ]] || false
  [[ ! -e "${WORK}/xcrun.log" ]] || false
}

@test "passes its arguments to CMake's configure step" {
  cat >"${WORK}/bin/cmake" <<'FAKE'
#!/bin/bash
echo "$*" >>"${WORK}/cmake.log"
FAKE
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh" -DFACEPALM_MEDIAPIPE_C_INCLUDE=x
  [[ "${status}" -eq 0 ]] || false
  configure="$(head -1 "${WORK}/cmake.log")"
  [[ "${configure}" == *'=ON -DFACEPALM_MEDIAPIPE_C_INCLUDE=x' ]] || false
}

@test "measures a unit in src/linux from its source there" {
  mkdir -p "${WORK}/repo/src/linux"
  touch "${WORK}/repo/src/linux/v4l2_session.cc"
  rm "${WORK}/repo/build-coverage/head_pose_test"
  touch "${WORK}/repo/build-coverage/v4l2_session_test"
  chmod +x "${WORK}/repo/build-coverage/v4l2_session_test"
  cat >"${WORK}/bin/llvm-cov" <<'FAKE'
#!/bin/bash
echo "$*" >>"${WORK}/cov.log"
cat "${WORK}/summary.json"
FAKE
  floors '"src/linux/v4l2_session.cc":
    {"unit": {"lines": 100, "branches": 100, "functions": 100}}'
  summary 10 90 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "$(<"${WORK}/cov.log")" == *'/repo/src/linux/v4l2_session.cc' ]] || false
  [[ "${output}" == *'src/linux/v4l2_session.cc: lines 90% is below 100%'* ]] \
    || false
}

@test "holds a file in src/linux with floors to a test executable on Linux" {
  floors '"src/head_pose.cc": {"unit": {"lines": 100}},
    "src/linux/v4l2_session.cc": {"unit": {"lines": 100}}'
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'src/linux/v4l2_session.cc: has floors but no test'* ]] \
    || false
}

@test "leaves the floors of files in src/linux out elsewhere" {
  floors '"src/head_pose.cc": {"unit": {"lines": 100}},
    "src/linux/v4l2_session.cc": {"unit": {"lines": 100}}'
  summary 10 100 4 100 2 100
  FAKE_UNAME=Darwin run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 0 ]] || false
}
