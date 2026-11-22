#!/bin/bash
#
# Guides the user through recording camera_probe fixtures for #139's eyes
# line: nine short sessions, each with four spoken cues CUE_SECONDS apart,
# saved as test/fixtures/eyes/<name>.probe. Each is camera_probe's full
# log, numbers only; camera_probe is never given a recording directory
# here, so it never writes images.
#
# Usage: record_eye_fixtures.sh
# Env (all optional, mainly for tests): CAMERA_PROBE, FACEPALM_MODEL,
#   RECORD_EYE_FIXTURES_FPS, RECORD_EYE_FIXTURES_CUE_SECONDS.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ROOT
readonly CAMERA_PROBE="${CAMERA_PROBE:-${ROOT}/build/camera_probe}"
readonly MODEL="${FACEPALM_MODEL:-${ROOT}/.claude/app-inputs/face_landmarker.task}"
readonly FPS="${RECORD_EYE_FIXTURES_FPS:-15}"
readonly CUE_SECONDS="${RECORD_EYE_FIXTURES_CUE_SECONDS:-6}"
readonly OUT_DIR="${ROOT}/test/fixtures/eyes"

# One fixture per entry: name|cue at 0|cue at CUE_SECONDS|cue at 2x|cue at
# 3x. Built with += a line at a time, so kcov's line coverage counts each
# one (#139; see coverage_shell.sh's own note on multi-line literals).
FIXTURES=()
FIXTURES+=('head_turn_eyes_centered|Look straight ahead, head level.|Turn your head left; keep your eyes on the camera.|Back to center.|Turn your head right; keep your eyes on the camera.')
FIXTURES+=('eyes_to_corners|Look straight ahead.|Look at the top-left corner without moving your head.|Back to center.|Look at the bottom-right corner without moving your head.')
FIXTURES+=('gaze_other_screen|Look at the screen in front of you.|Keep your head still; move your eyes to your other screen.|Back to the screen in front of you.|Look at your other screen again, for longer this time.')
FIXTURES+=('blinks|Look straight ahead normally.|Blink a few times, as you would while reading.|Rest your eyes open for a moment.|Blink a few more times.')
FIXTURES+=('fully_away|Look at the camera.|Turn away completely, as if someone called your name.|Come back and look at the camera.|Turn away completely again, and stay turned away.')
FIXTURES+=('glance_keyboard|Look at the screen.|Glance down at the keyboard.|Look at the screen again.|Glance down at the keyboard once more, and hold it there.')
FIXTURES+=('thinking_aversion|Look at the camera, as if just asked a question.|Look away to think, as if answering a hard question.|Look back at the camera with your answer.|Look away to think again, for an even harder question.')
FIXTURES+=('reading_other_screen|Look at the screen in front of you.|Read something on your other screen for a while.|Keep reading the other screen.|Look back at the screen in front of you.')
FIXTURES+=('long_blinks|Look straight ahead normally.|Close your eyes slowly, hold a couple seconds, then open.|Rest your eyes open for a moment.|Close your eyes slowly again, hold a few seconds, then open.')
readonly FIXTURES

fail() {
  echo "$*" >&2
  exit 1
}

# Records one fixture: camera_probe runs for 4 * CUE_SECONDS while the four
# cues print in turn, then its full log is saved to OUT_DIR/name.probe.
# Globals: CAMERA_PROBE, MODEL, FPS, CUE_SECONDS, OUT_DIR
record_one() {
  local name="$1" cue0="$2" cue6="$3" cue12="$4" cue18="$5"
  local log
  log="$(mktemp)"
  echo
  echo "=== ${name} ==="
  read -r -p "Press Enter when ready to record (${CUE_SECONDS}x4 seconds)... " _
  "${CAMERA_PROBE}" "$((CUE_SECONDS * 4))" "${FPS}" "${MODEL}" >"${log}" &
  local pid=$!
  echo "${cue0}"
  sleep "${CUE_SECONDS}"
  echo "${cue6}"
  sleep "${CUE_SECONDS}"
  echo "${cue12}"
  sleep "${CUE_SECONDS}"
  echo "${cue18}"
  if ! wait "${pid}"; then
    rm -f "${log}"
    fail "camera_probe failed recording ${name}"
  fi
  mkdir -p "${OUT_DIR}"
  mv "${log}" "${OUT_DIR}/${name}.probe"
  echo "Saved ${OUT_DIR}/${name}.probe"
}

main() {
  [[ -x "${CAMERA_PROBE}" ]] || fail "no camera_probe at ${CAMERA_PROBE}"
  [[ -f "${MODEL}" ]] || fail "no face model at ${MODEL}"
  local entry name cue0 cue6 cue12 cue18
  for entry in "${FIXTURES[@]}"; do
    IFS='|' read -r name cue0 cue6 cue12 cue18 <<<"${entry}"
    record_one "${name}" "${cue0}" "${cue6}" "${cue12}" "${cue18}"
  done
  echo "All fixtures recorded in ${OUT_DIR}"
}

main "$@"
