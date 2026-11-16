#!/usr/bin/env bats
#
# Tests specs/check.sh against a fake TLC, given through the TLC variable.

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/specs"
  cp "${BATS_TEST_DIRNAME}/../check.sh" "${WORK}/specs/"
  touch "${WORK}/specs/Camera.tla" "${WORK}/specs/Camera.cfg"
  # Logs its arguments, prints FAKE_OUT and exits with FAKE_CODE.
  cat >"${WORK}/tlc" <<'EOF'
#!/bin/bash
echo "$*" >>"${LOG}"
echo "${FAKE_OUT}"
exit "${FAKE_CODE}"
EOF
  chmod +x "${WORK}/tlc"
  export TLC="${WORK}/tlc" LOG="${WORK}/log" FAKE_CODE=0
  export FAKE_OUT='Model checking completed. No error has been found.'
}

teardown() {
  rm -rf "${WORK}"
}

@test "passes when TLC finds no error" {
  run bash "${WORK}/specs/check.sh"
  [[ "${status}" -eq 0 ]]
  [[ "${output}" == *'Camera.cfg'*'pass'* ]]
}

@test "fails, with TLC's output, when TLC finds a violation" {
  FAKE_OUT='Error: Invariant OpenOnlyWithConsent is violated.' FAKE_CODE=12
  run bash "${WORK}/specs/check.sh"
  [[ "${status}" -eq 1 ]]
  [[ "${output}" == *'FAILED'* ]]
  [[ "${output}" == *'OpenOnlyWithConsent is violated'* ]]
}

@test "fails when TLC exits non-zero even without reporting a violation" {
  FAKE_CODE=1
  run bash "${WORK}/specs/check.sh"
  [[ "${status}" -eq 1 ]]
}

@test "fails when TLC exits 0 without saying no error was found" {
  FAKE_OUT='Parsing file Camera.tla'
  run bash "${WORK}/specs/check.sh"
  [[ "${status}" -eq 1 ]]
}

@test "checks a named config against the spec before its underscore" {
  touch "${WORK}/specs/Camera_Restart.cfg"
  run bash "${WORK}/specs/check.sh"
  [[ "${status}" -eq 0 ]]
  log="$(cat "${LOG}")"
  [[ "${log}" == *'-config Camera_Restart.cfg Camera.tla'* ]]
  [[ "${log}" == *'-config Camera.cfg Camera.tla'* ]]
}
