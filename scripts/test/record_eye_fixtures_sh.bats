#!/usr/bin/env bats
#
# Tests scripts/record_eye_fixtures.sh against a fake camera_probe, with
# RECORD_EYE_FIXTURES_CUE_SECONDS=0 so it never really waits.

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/scripts"
  cp "${BATS_TEST_DIRNAME}/../record_eye_fixtures.sh" "${WORK}/scripts/"
  SCRIPT="${WORK}/scripts/record_eye_fixtures.sh"
  CALLS="${WORK}/calls"
  : >"${CALLS}"
  cat >"${WORK}/camera_probe" <<'BIN'
#!/bin/bash
echo "$*" >>"${CALLS}"
echo "seconds $1"
echo "eyes 10 0.1"
echo "blendshapes 10 0.5"
echo "gesture_raw 10 1 2"
echo "gesture_probs 10 0.9"
BIN
  chmod +x "${WORK}/camera_probe"
  touch "${WORK}/model.task"
  export WORK CALLS
  export CAMERA_PROBE="${WORK}/camera_probe"
  export FACEPALM_MODEL="${WORK}/model.task"
  export RECORD_EYE_FIXTURES_FPS=15 RECORD_EYE_FIXTURES_CUE_SECONDS=0
}

# Runs the script, feeding its read -p prompts an endless stream of blank
# lines: it reads one per fixture and never sees the end of the stream.
run_script() {
  # shellcheck disable=SC2312 # yes never fails; the script closes its pipe.
  run bash "${SCRIPT}" < <(yes '')
}

teardown() {
  rm -rf "${WORK}"
}

@test "records all ten fixtures, each with its own probe log and cues" {
  run_script
  [[ "${status}" -eq 0 ]] || false
  [[ "${output}" == *'=== head_turn_eyes_centered ==='* ]] || false
  [[ "${output}" == *'Look at the top-left corner without moving your head.'* ]] \
    || false
  [[ "${output}" == *'=== widened_eyes ==='* ]] || false
  [[ "${output}" == *'All fixtures recorded in'* ]] || false
  local fixtures="${WORK}/test/fixtures/eyes" probes probe_count calls
  [[ -f "${fixtures}/head_turn_eyes_centered.probe" ]] || false
  [[ -f "${fixtures}/long_blinks.probe" ]] || false
  [[ -f "${fixtures}/widened_eyes.probe" ]] || false
  probes="$(find "${fixtures}" -name '*.probe')"
  probe_count="$(wc -l <<<"${probes}")"
  calls="$(wc -l <"${CALLS}")"
  [[ "${probe_count}" -eq 10 ]] || false
  [[ "$(<"${fixtures}/blinks.probe")" == *'eyes 10 0.1'* ]] || false
  [[ "$(<"${fixtures}/blinks.probe")" != *'blendshapes'* ]] || false
  [[ "$(<"${fixtures}/blinks.probe")" != *'gesture_raw'* ]] || false
  [[ "$(<"${fixtures}/blinks.probe")" != *'gesture_probs'* ]] || false
  [[ "${calls}" -eq 10 ]] || false
}

@test "passes the configured seconds, fps and model to camera_probe" {
  export RECORD_EYE_FIXTURES_CUE_SECONDS=1
  local first_call
  run_script
  [[ "${status}" -eq 0 ]] || false
  first_call="$(head -1 "${CALLS}")"
  [[ "${first_call}" == "4 15 ${WORK}/model.task" ]] || false
}

@test "fails without running anything when camera_probe is missing" {
  run env -u CAMERA_PROBE PATH="${PATH}" FACEPALM_MODEL="${FACEPALM_MODEL}" \
    bash "${SCRIPT}"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'no camera_probe at'* ]] || false
  [[ ! -s "${CALLS}" ]] || false
}

@test "fails without running anything when the model is missing" {
  run env FACEPALM_MODEL="${WORK}/missing.task" bash "${SCRIPT}"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'no face model at'* ]] || false
  [[ ! -s "${CALLS}" ]] || false
}

@test "fails and saves nothing once camera_probe fails" {
  cat >"${WORK}/camera_probe" <<'BIN'
#!/bin/bash
echo "$*" >>"${CALLS}"
exit 1
BIN
  chmod +x "${WORK}/camera_probe"
  run_script
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'camera_probe failed recording head_turn_eyes_centered'* ]] \
    || false
  [[ ! -e "${WORK}/test/fixtures/eyes/head_turn_eyes_centered.probe" ]] || false
}
