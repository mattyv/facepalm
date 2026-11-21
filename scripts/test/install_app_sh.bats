#!/usr/bin/env bats

setup() {
  WORK="$(mktemp -d "${TMPDIR:-/tmp}/facepalm-install.XXXXXX")"
  mkdir -p "${WORK}/Facepalm.app/Contents/MacOS" "${WORK}/destination"
  printf app >"${WORK}/Facepalm.app/Contents/MacOS/facepalm-daemon"
}

teardown() {
  rm -rf "${WORK}"
}

@test "copies the app to the chosen directory without starting it" {
  run bash "${BATS_TEST_DIRNAME}/../install_app.sh" \
    "${WORK}/Facepalm.app" "${WORK}/destination"
  [[ "${status}" -eq 0 ]] || false
  [[ -f "${WORK}/destination/Facepalm.app/Contents/MacOS/facepalm-daemon" ]] \
    || false
}

@test "never overwrites an existing installation" {
  mkdir "${WORK}/destination/Facepalm.app"
  printf keep >"${WORK}/destination/Facepalm.app/marker"
  run bash "${BATS_TEST_DIRNAME}/../install_app.sh" \
    "${WORK}/Facepalm.app" "${WORK}/destination"
  [[ "${status}" -ne 0 ]] || false
  run cat "${WORK}/destination/Facepalm.app/marker"
  [[ "${output}" = keep ]] || false
}

@test "removes a partial installation when copying fails" {
  mkdir "${WORK}/bin"
  cat >"${WORK}/bin/cp" <<'TOOL'
#!/bin/bash
mkdir -p "$3"
exit 1
TOOL
  chmod +x "${WORK}/bin/cp"
  export PATH="${WORK}/bin:${PATH}"
  run bash "${BATS_TEST_DIRNAME}/../install_app.sh" \
    "${WORK}/Facepalm.app" "${WORK}/destination"
  [[ "${status}" -ne 0 ]] || false
  [[ ! -e "${WORK}/destination/Facepalm.app" ]] || false
}

@test "prints its usage when the destination is not a directory" {
  run bash "${BATS_TEST_DIRNAME}/../install_app.sh" \
    "${WORK}/Facepalm.app" "${WORK}/missing"
  [[ "${status}" -eq 2 ]] || false
  [[ "${output}" == 'usage: install_app.sh '* ]] || false
}
