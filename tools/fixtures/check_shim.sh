#!/bin/bash
#
# Checks libfacepalm_mediapipe.dylib, as tools/fixtures/build.sh built it: it
# must export facepalm_mediapipe.h's six functions and nothing else, so the
# abseil and protobuf inside cannot clash with another copy in the process,
# and must link no OpenCV dylib, which is linked in statically. Prints its
# size.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly SHIM="${ROOT}/build-mediapipe/mediapipe/bazel-bin/facepalm_shim"
readonly DYLIB="${SHIM}/libfacepalm_mediapipe.dylib"
readonly INTERFACE=(_fp_face_model_create _fp_face_model_destroy
  _fp_face_model_detect _fp_face_detector_create _fp_face_detector_destroy
  _fp_face_detector_detect)

main() {
  local size exports links failed=0 symbol name library
  size="$(wc -c <"${DYLIB}")"
  echo "libfacepalm_mediapipe.dylib: $((size)) bytes"
  exports="$(nm -gU "${DYLIB}")"
  links="$(otool -L "${DYLIB}")"
  while read -r _ _ symbol; do
    [[ -z "${symbol}" ]] && continue
    if [[ " ${INTERFACE[*]} " != *" ${symbol} "* ]]; then
      echo "exports ${symbol}" >&2
      failed=1
    fi
  done <<<"${exports}"
  for name in "${INTERFACE[@]}"; do
    if [[ $'\n'"${exports}"$'\n' != *" ${name}"$'\n'* ]]; then
      echo "does not export ${name}" >&2
      failed=1
    fi
  done
  while read -r library _; do
    if [[ "${library}" == *libopencv* ]]; then
      echo "links ${library}" >&2
      failed=1
    fi
  done <<<"${links}"
  return "${failed}"
}

main "$@"
