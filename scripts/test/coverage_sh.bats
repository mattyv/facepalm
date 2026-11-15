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
  # Stands in for "xcrun --find <tool>", answering with the tool on the PATH.
  cat >"${WORK}/bin/xcrun" <<'EOF'
#!/bin/bash
echo "$2"
EOF
  printf '#!/bin/bash\ncat "%s"\n' "${WORK}/summary.json" \
    >"${WORK}/bin/llvm-cov"
  chmod +x "${WORK}"/bin/*
  PATH="${WORK}/bin:${PATH}"
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
  [[ "${status}" -eq 0 ]]
}

@test "fails, naming the file and measure, when one is below its floor" {
  summary 10 90 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]]
  [[ "${output}" == *'src/head_pose.cc: lines 90% is below 100%'* ]]
}

@test "counts a measure with nothing to count as full" {
  summary 10 100 0 0 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 0 ]]
}

@test "holds a unit with no floors listed to 100" {
  floors ''
  summary 10 100 4 75 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]]
  [[ "${output}" == *'branches 75% is below 100%'* ]]
}

@test "fails when a unit's profiles cannot be merged" {
  printf '#!/bin/bash\nexit 1\n' >"${WORK}/bin/llvm-profdata"
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -ne 0 ]]
}

@test "fails when a file has floors but no test executable" {
  rm "${WORK}/repo/build-coverage/head_pose_test"
  touch "${WORK}/repo/build-coverage/other_test"
  chmod +x "${WORK}/repo/build-coverage/other_test"
  summary 10 100 4 100 2 100
  run bash "${WORK}/repo/scripts/coverage.sh"
  [[ "${status}" -eq 1 ]]
  [[ "${output}" == *'src/head_pose.cc: has floors but no test executable'* ]]
}
