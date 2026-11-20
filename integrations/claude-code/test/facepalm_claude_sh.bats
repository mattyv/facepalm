#!/usr/bin/env bats

setup() {
  WORK="$(mktemp -d "${TMPDIR:-/tmp}/fph-test.XXXXXX")"
  mkdir -p "${WORK}/bin"
  cat >"${WORK}/bin/claude" <<'SCRIPT'
#!/bin/bash
printf '%s\n' "${FACEPALM_HOOK_SOCKET}" >"${RECORD}"
[[ -d "$(dirname "${FACEPALM_HOOK_SOCKET}")" ]] || exit 2
exit 7
SCRIPT
  chmod +x "${WORK}/bin/claude"
  export PATH="${WORK}/bin:${PATH}"
  export RECORD="${WORK}/record"
  FACEPALM_NODE="$(command -v node)"
  export FACEPALM_NODE
}

teardown() {
  rm -rf "${WORK}"
}

@test "creates a private per-run socket directory and cleans it after Claude exits" {
  run bash "${BATS_TEST_DIRNAME}/../facepalm-claude.sh"
  [[ "${status}" -eq 7 ]] || false
  socket="$(cat "${RECORD}")"
  [[ ! -e "$(dirname "${socket}")" ]] || false
}

@test "passes stdin through to the Claude process" {
  cat >"${WORK}/bin/claude" <<'SCRIPT'
#!/bin/bash
IFS= read -r line || exit 2
[[ "${line}" = hello ]] || exit 3
SCRIPT
  chmod +x "${WORK}/bin/claude"
  run bash "${BATS_TEST_DIRNAME}/../facepalm-claude.sh" <<<"hello"
  [[ "${status}" -eq 0 ]] || false
}
