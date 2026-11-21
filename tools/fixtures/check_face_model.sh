#!/bin/bash
#
# Holds face_model_check, facepalm's MediaPipeFaceModel over the real
# libfacepalm_mediapipe.dylib, to face_landmarker_fixtures, MediaPipe's own
# face landmarker in VIDEO mode, as tools/fixtures/build.sh built them. Both
# read a lossless PNG of MediaPipe's portrait, so their pixels are the same,
# and every landmark, blendshape and pose value must match exactly. (From the
# JPEG they differ, by up to 1.6 mm in the pose: macOS's JPEG decoder is not
# MediaPipe's.)
#
# Usage: check_face_model.sh <face_model_check>

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly WORK="${ROOT}/build-mediapipe"
readonly DATA="${WORK}/data"
readonly MODEL="${DATA}/face_landmarker_v2_with_blendshapes.task"
readonly FIXTURES="${WORK}/mediapipe/bazel-bin/facepalm_fixtures"
readonly TOOL="${FIXTURES}/face_landmarker_fixtures"
# The face's values in order, without the image's path or blendshape names.
VALUES='.[0] | .landmarks[][], (.blendshapes | to_entries[].value),'
VALUES+=' .pose_column_major[]'
readonly VALUES

main() {
  if (($# != 1)); then
    echo "usage: check_face_model.sh <face_model_check>" >&2
    return 2
  fi
  local png="${DATA}/portrait.png"
  sips -s format png "${DATA}/portrait.jpg" --out "${png}" >/dev/null
  "${TOOL}" --video "${MODEL}" "${png}" 2>/dev/null \
    | jq -r "${VALUES}" >"${DATA}/face_model.expected"
  "$1" "${MODEL}" "${png}" | jq -r "${VALUES}" >"${DATA}/face_model.actual"
  local count
  count="$(wc -l <"${DATA}/face_model.expected")"
  if ! cmp -s "${DATA}/face_model.expected" "${DATA}/face_model.actual"; then
    echo "face_model_check differs from face_landmarker_fixtures:" >&2
    diff "${DATA}/face_model.expected" "${DATA}/face_model.actual" \
      | head -n 10 >&2 || true
    return 1
  fi
  echo "face_model_check matches face_landmarker_fixtures: $((count)) values"
}

main "$@"
