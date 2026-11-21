#!/bin/bash
#
# Runs face_landmarker_fixtures, as tools/fixtures/build.sh built it, on
# MediaPipe's portrait test image, and fails unless the result matches
# MediaPipe's own expected landmarks, blendshapes and transformation matrix
# within the margins MediaPipe's face_landmarker_test.cc allows.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly WORK="${ROOT}/build-mediapipe"
readonly MEDIAPIPE="${WORK}/mediapipe"
readonly BIN="${MEDIAPIPE}/bazel-bin/facepalm_fixtures"
readonly TOOL="${BIN}/face_landmarker_fixtures"
readonly GOLDEN="${MEDIAPIPE}/mediapipe/tasks/testdata/vision"
readonly LANDMARK_MARGIN=0.03
readonly BLENDSHAPE_MARGIN=0.12
readonly MATRIX_MARGIN=0.04

#######################################
# Prints how many pairs differ by more than a margin, counting a value
# missing from either side as one.
# Arguments:
#   Margin, file of actual values, file of expected values, one per line.
# Outputs:
#   The count of values outside the margin.
#######################################
outside() {
  local program
  read -r -d '' program <<'AWK' || true
{
  if ($1 == "" || $2 == "") { count++; next }
  d = $1 - $2; if (d < 0) d = -d
  if (d > margin) count++
}
END { print count + 0 }
AWK
  paste -d ' ' "$2" "$3" | awk -v margin="$1" "${program}"
}

main() {
  local out="${WORK}/data"
  "${TOOL}" "${out}/face_landmarker_v2_with_blendshapes.task" \
    "${out}/portrait.jpg" >"${out}/portrait.json" 2>"${out}/portrait.log"
  jq -r '.[0].landmarks[] | .[0], .[1]' "${out}/portrait.json" \
    >"${out}/landmarks.txt"
  awk '/^ *[xy]:/ { print $2 }' \
    "${GOLDEN}/portrait_expected_face_landmarks.pbtxt" \
    >"${out}/landmarks.golden"
  jq -r '.[0].blendshapes | to_entries[] | .value' "${out}/portrait.json" \
    >"${out}/blendshapes.txt"
  awk '/score:/ { print $2 }' "${GOLDEN}/portrait_expected_blendshapes.pbtxt" \
    >"${out}/blendshapes.golden"
  jq -r '.[0].pose_column_major[]' "${out}/portrait.json" >"${out}/pose.txt"
  awk '/packed_data:/ { print $2 }' \
    "${GOLDEN}/portrait_expected_face_geometry.pbtxt" >"${out}/pose.golden"

  local failed=0 name margin count
  for name in landmarks:"${LANDMARK_MARGIN}" \
    blendshapes:"${BLENDSHAPE_MARGIN}" pose:"${MATRIX_MARGIN}"; do
    margin="${name#*:}"
    name="${name%%:*}"
    count="$(outside "${margin}" "${out}/${name}.txt" "${out}/${name}.golden")"
    if ((count > 0)); then
      echo "${name}: ${count} values outside ${margin}" >&2
      failed=1
    fi
  done
  return "${failed}"
}

main "$@"
