#!/bin/bash
#
# Builds facepalm with coverage, runs every unit's tests, and holds each
# source file to its unit floors in coverage_thresholds.json from its tests.

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
# Writes a unit's coverage to profiles/<unit>.tsv as "lines branches functions"
# percentages. A measure with nothing to count, such as branches in
# straight-line code, is 100.
# Globals:
#   BUILD, PROFILES, ROOT, SUMMARY
# Arguments:
#   The unit's name, such as head_pose.
#######################################
measure() {
  local unit="$1" profdata cov
  profdata="$(llvm_tool llvm-profdata)"
  cov="$(llvm_tool llvm-cov)"
  "${profdata}" merge -sparse "${PROFILES}/${unit}"-*.profraw \
    -o "${PROFILES}/${unit}.profdata"
  "${cov}" export -summary-only -instr-profile="${PROFILES}/${unit}.profdata" \
    "${BUILD}/${unit}_test" "${ROOT}/src/${unit}.cc" \
    | jq -r "${SUMMARY}" >"${PROFILES}/${unit}.tsv"
}

#######################################
# Prints each measure of a unit that is below its floor.
# Globals:
#   MEASURES, PROFILES, THRESHOLDS
# Arguments:
#   The unit's name.
#######################################
check_unit() {
  local file="src/$1.cc" i floor
  local -a actual query
  read -r -a actual <"${PROFILES}/$1.tsv"
  for i in "${!MEASURES[@]}"; do
    query=(-r --arg f "${file}" --arg m "${MEASURES[i]}")
    floor="$(jq "${query[@]}" '.[$f].unit[$m] // 100' "${THRESHOLDS}")"
    if awk -v a="${actual[i]}" -v f="${floor}" 'BEGIN { exit !(a < f) }'; then
      echo "${file}: ${MEASURES[i]} ${actual[i]}% is below ${floor}%"
    fi
  done
}

main() {
  cmake -S "${ROOT}" -B "${BUILD}" -G Ninja -DFACEPALM_COVERAGE=ON
  cmake --build "${BUILD}"
  rm -rf "${PROFILES}"
  ctest --test-dir "${BUILD}" --output-on-failure
  mkdir -p "${PROFILES}"
  local test_binary unit listed listed_files units=' '
  local shortfalls="${PROFILES}/shortfalls.txt"
  for test_binary in "${BUILD}"/*_test; do
    unit="$(basename "${test_binary}" _test)"
    units+="${unit} "
    measure "${unit}"
    check_unit "${unit}" >>"${shortfalls}"
  done
  listed_files="$(jq -r 'map_values(.unit // empty) | keys[]' "${THRESHOLDS}")"
  while read -r listed; do
    unit="${listed#src/}"
    if [[ -n "${listed}" && "${units}" != *" ${unit%.cc} "* ]]; then
      echo "${listed}: has floors but no test executable" >>"${shortfalls}"
    fi
  done <<<"${listed_files}"
  if [[ -s "${shortfalls}" ]]; then
    cat "${shortfalls}" >&2
    return 1
  fi
}

main "$@"
