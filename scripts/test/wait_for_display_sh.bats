#!/usr/bin/env bats
#
# Tests scripts/wait_for_display.sh against a fake xdpyinfo on the PATH,
# which fails FAIL_COUNT times (from a counter file) before succeeding.

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/bin"
  CALLS="${WORK}/calls"
  COUNTER="${WORK}/counter"
  export CALLS COUNTER
  : >"${COUNTER}"
  cat >"${WORK}/bin/xdpyinfo" <<'TOOL'
#!/bin/bash
echo "$*" >>"${CALLS}"
tries="$(wc -l <"${COUNTER}")"
printf 'x\n' >>"${COUNTER}"
(("${tries}" >= "${FAIL_COUNT}"))
TOOL
  chmod +x "${WORK}/bin/xdpyinfo"
  cat >"${WORK}/bin/marker-command" <<'TOOL'
#!/bin/bash
echo "ran: $*" >>"${CALLS}"
TOOL
  chmod +x "${WORK}/bin/marker-command"
  export PATH="${WORK}/bin:${PATH}"
  export WAIT_FOR_DISPLAY_MAX_ATTEMPTS=3 WAIT_FOR_DISPLAY_POLL_SECONDS=0
  SCRIPT="${BATS_TEST_DIRNAME}/../wait_for_display.sh"
}

teardown() {
  rm -rf "${WORK}"
}

@test "runs the command once the display accepts connections" {
  FAIL_COUNT=0 DISPLAY=:99 run bash "${SCRIPT}" marker-command one two
  [[ "${status}" -eq 0 ]] || false
  calls="$(<"${CALLS}")"
  [[ "${calls}" == *'-display :99'* ]] || false
  [[ "${calls}" == *'ran: one two'* ]] || false
}

@test "polls until a late display accepts connections" {
  FAIL_COUNT=2 DISPLAY=:5 run bash "${SCRIPT}" marker-command
  [[ "${status}" -eq 0 ]] || false
  tries="$(wc -l <"${COUNTER}")"
  [[ "${tries}" -eq 3 ]] || false
  [[ "$(<"${CALLS}")" == *'ran:'* ]] || false
}

@test "fails once the attempts run out, and never runs the command" {
  FAIL_COUNT=99 DISPLAY=:7 run bash "${SCRIPT}" marker-command
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'did not accept connections'* ]] || false
  [[ "$(<"${CALLS}")" != *'ran:'* ]] || false
}

@test "fails without running anything when DISPLAY is unset" {
  run env -u DISPLAY PATH="${PATH}" bash "${SCRIPT}" marker-command
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'DISPLAY is not set'* ]] || false
}

@test "prints its usage without a command" {
  run env DISPLAY=:99 bash "${SCRIPT}"
  [[ "${status}" -eq 2 ]] || false
  [[ "${output}" == *'usage:'* ]] || false
}
