#!/usr/bin/env bats
#
# Tests scripts/check_app_links.sh against a fake otool on the PATH, which
# prints LINKS for -L and RPATHS for -l, with $2 standing for the binary.

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/bin"
  cat >"${WORK}/bin/otool" <<'TOOL'
#!/bin/bash
if [[ "$1" == -l ]]; then
  printf '%b' "${RPATHS}"
  exit 0
fi
printf '%s:\n' "$2"
printf '%b' "${LINKS}"
if [[ "$2" == daemon ]]; then
  printf '\t@rpath/libfacepalm_mediapipe.dylib (compatibility version 1)\n'
fi
TOOL
  chmod +x "${WORK}/bin/otool"
  export PATH="${WORK}/bin:${PATH}"
  export LINKS='\t/usr/lib/libSystem.B.dylib (compatibility version 1)\n'
  FRAMEWORKS='  cmd LC_RPATH\n'
  export FRAMEWORKS+='  path @executable_path/../Frameworks (o 1)\n'
  export RPATHS="${FRAMEWORKS}"
  SCRIPT="${BATS_TEST_DIRNAME}/../check_app_links.sh"
}

teardown() {
  rm -rf "${WORK}"
}

@test "accepts system links and a daemon that finds the shim in the app" {
  run bash "${SCRIPT}" menu daemon shim node
  [[ "${status}" -eq 0 ]] || false
}

@test "rejects a link outside the system" {
  LINKS='\t/opt/homebrew/lib/libnode.dylib (compatibility version 1)\n'
  run bash "${SCRIPT}" menu daemon shim node
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'non-relocatable dependency in menu'* ]] || false
}

@test "rejects a daemon with no search path" {
  RPATHS=''
  run bash "${SCRIPT}" menu daemon shim node
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *"through the app's Frameworks"* ]] || false
}

@test "rejects a daemon that searches elsewhere first" {
  RPATHS="  cmd LC_RPATH\n  path /tmp/other (o 1)\n${FRAMEWORKS}"
  run bash "${SCRIPT}" menu daemon shim node
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *"through the app's Frameworks"* ]] || false
}

@test "rejects a daemon that does not link the shim" {
  run bash "${SCRIPT}" menu other shim node
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *"through the app's Frameworks"* ]] || false
}

@test "rejects a Node that links the shim" {
  run bash "${SCRIPT}" menu daemon shim daemon
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'unexpected shim dependency in Node'* ]] || false
}

@test "prints its usage without four arguments" {
  run bash "${SCRIPT}" menu daemon
  [[ "${status}" -eq 2 ]] || false
  [[ "${output}" == *'usage:'* ]] || false
}
