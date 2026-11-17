#!/bin/bash
#
# Runs the shim's tests against the real face landmarker, with the shim's own
# source instrumented, and fails unless they pass and cover all of it, as
# scripts/coverage.sh holds facepalm's CMake sources. Needs
# tools/fixtures/build.sh to have run. Passes DEVELOPER_DIR and
# BAZEL_DISK_CACHE to Bazel, as build.sh does.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly WORK="${ROOT}/build-mediapipe"
readonly SOURCE="facepalm_shim/facepalm_mediapipe.cc"
readonly TEST="bazel-bin/facepalm_shim/facepalm_mediapipe_test"
readonly BAZEL="${BAZEL:-bazelisk}"

#######################################
# Prints each measure of SOURCE below 100 percent.
# Arguments:
#   llvm-cov's report.
# Outputs:
#   One line per shortfall.
#######################################
shortfalls() {
  awk -v source="${SOURCE}" '$1 == source {
    split("regions functions lines branches", names, " ")
    for (i = 1; i <= 4; i++) {
      cover = $(1 + 3 * i)
      if (cover != "100.00%" && cover != "-") {
        printf "%s: %s %s\n", source, names[i], cover
      }
    }
    found = 1
  } END { if (!found) printf "%s: not in the report\n", source }' <<<"$1"
}

main() {
  local flags=() flag
  if [[ -n "${DEVELOPER_DIR:-}" ]]; then
    for flag in repo_env action_env host_action_env; do
      flags+=("--${flag}=DEVELOPER_DIR=${DEVELOPER_DIR}")
    done
  fi
  if [[ -n "${BAZEL_DISK_CACHE:-}" ]]; then
    flags+=("--disk_cache=${BAZEL_DISK_CACHE}")
  fi
  cd "${WORK}/mediapipe"
  "${BAZEL}" --bazelrc="${ROOT}/tools/fixtures/mediapipe.bazelrc" build \
    ${flags[@]+"${flags[@]}"} \
    "--per_file_copt=${SOURCE}@-fprofile-instr-generate,-fcoverage-mapping" \
    --linkopt=-fprofile-instr-generate \
    //facepalm_shim:facepalm_mediapipe_test
  local profiles="${WORK}/shim-profiles"
  rm -rf "${profiles}"
  FACEPALM_DATA="${WORK}/data" LLVM_PROFILE_FILE="${profiles}/%p.profraw" \
    "./${TEST}"
  xcrun llvm-profdata merge -o "${profiles}/merged.profdata" \
    "${profiles}"/*.profraw
  local report missing
  report="$(xcrun llvm-cov report "./${TEST}" \
    -instr-profile="${profiles}/merged.profdata")"
  missing="$(shortfalls "${report}")"
  if [[ -n "${missing}" ]]; then
    echo "${missing}" >&2
    return 1
  fi
}

main "$@"
