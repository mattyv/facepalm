#!/bin/bash
#
# Builds facepalm with coverage, runs every unit's tests, and holds each
# source file to its unit floors in coverage_thresholds.json from its tests.
# Arguments go to CMake's configure step, such as the MediaPipe C paths that
# add the units in src/linux (#83), whose floors count on Linux only.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ROOT
readonly BUILD="${ROOT}/build-coverage"
readonly PROFILES="${BUILD}/profiles"
readonly THRESHOLDS="${ROOT}/coverage_thresholds.json"
readonly MEASURES=(lines branches functions)
SUMMARY='.data[0].files[0].summary | [.lines, .branches, .functions]'
SUMMARY+=' | map(if .count == 0 then 100 else .percent end) | @tsv'
readonly SUMMARY
LISTED='map_values(.unit // empty) | keys[]'
# shellcheck disable=SC2016 # $os is jq's own variable.
LISTED+=' | select($os == "Linux" or (startswith("src/linux/") | not))'
readonly LISTED

#######################################
# Prints the path of an LLVM tool that matches the compiler: xcrun's on macOS.
# Arguments:
#   The tool's name.
#######################################
llvm_tool() {
  local os
  os="$(uname)"
  if [[ "${os}" == 'Darwin' ]]; then
    xcrun --find "$1"
  else
    echo "$1"
  fi
}

#######################################
# Measures a unit's coverage from its own tests, writing it to
# profiles/<unit>.tsv as "lines branches functions" percentages, and prints
# each measure below its floor. A measure with nothing to count, such as
# branches in straight-line code, is 100.
# Globals:
#   BUILD, MEASURES, PROFILES, ROOT, SUMMARY, THRESHOLDS
# Arguments:
#   The unit's name, such as head_pose, and its source file, from the root.
#######################################
measure() {
  local unit="$1" file="$2" profdata cov i floor
  local -a actual query
  profdata="$(llvm_tool llvm-profdata)"
  cov="$(llvm_tool llvm-cov)"
  "${profdata}" merge -sparse "${PROFILES}/${unit}"-*.profraw \
    -o "${PROFILES}/${unit}.profdata"
  "${cov}" export -summary-only -instr-profile="${PROFILES}/${unit}.profdata" \
    "${BUILD}/${unit}_test" "${ROOT}/${file}" \
    | jq -r "${SUMMARY}" >"${PROFILES}/${unit}.tsv"
  read -r -a actual <"${PROFILES}/${unit}.tsv"
  for i in "${!MEASURES[@]}"; do
    query=(-r --arg f "${file}" --arg m "${MEASURES[i]}")
    floor="$(jq "${query[@]}" '.[$f].unit[$m] // 100' "${THRESHOLDS}")"
    if awk -v a="${actual[i]}" -v f="${floor}" 'BEGIN { exit !(a < f) }'; then
      echo "${file}: ${MEASURES[i]} ${actual[i]}% is below ${floor}%"
    fi
  done
}

main() {
  cmake -S "${ROOT}" -B "${BUILD}" -G Ninja -DFACEPALM_COVERAGE=ON "$@"
  cmake --build "${BUILD}"
  rm -rf "${PROFILES}"
  ctest --test-dir "${BUILD}" --output-on-failure
  mkdir -p "${PROFILES}"
  local test_binary unit file listed listed_files os files=' '
  local shortfalls="${PROFILES}/shortfalls.txt"
  for test_binary in "${BUILD}"/*_test; do
    unit="$(basename "${test_binary}" _test)"
    file="src/linux/${unit}.cc"
    [[ -e "${ROOT}/${file}" ]] || file="src/${unit}.cc"
    files+="${file} "
    measure "${unit}" "${file}" >>"${shortfalls}"
  done
  os="$(uname)"
  listed_files="$(jq -r --arg os "${os}" "${LISTED}" "${THRESHOLDS}")"
  while read -r listed; do
    if [[ -n "${listed}" && "${files}" != *" ${listed} "* ]]; then
      echo "${listed}: has floors but no test executable" >>"${shortfalls}"
    fi
  done <<<"${listed_files}"
  if [[ -s "${shortfalls}" ]]; then
    cat "${shortfalls}" >&2
    return 1
  fi
}

main "$@"
