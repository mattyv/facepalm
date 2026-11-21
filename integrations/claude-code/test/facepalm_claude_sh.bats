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

@test "makes a private per-run socket directory, removed after Claude exits" {
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

@test "fails, saying what to set, when Node is not executable" {
  FACEPALM_NODE="${WORK}/missing" run bash \
    "${BATS_TEST_DIRNAME}/../facepalm-claude.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == 'Set FACEPALM_NODE to an executable Node'* ]] || false
  [[ ! -e "${RECORD}" ]] || false
}

@test "stops Claude and cleans up when it is terminated" {
  cat >"${WORK}/bin/claude" <<'SCRIPT'
#!/bin/bash
printf '%s %s\n' "$$" "${FACEPALM_HOOK_SOCKET}" >"${RECORD}.tmp"
mv "${RECORD}.tmp" "${RECORD}"
exec sleep 30
SCRIPT
  bash "${BATS_TEST_DIRNAME}/../facepalm-claude.sh" &
  local wrapper=$! tries=0
  while [[ ! -s "${RECORD}" ]] && ((tries++ < 100)); do
    sleep 0.1
  done
  local claude socket
  read -r claude socket <"${RECORD}"
  local started="${SECONDS}" status=0
  kill -TERM "${wrapper}"
  wait "${wrapper}" || status=$?
  [[ "${status}" -eq 143 ]] || false
  # Well before Claude's 30 seconds: the wrapper stopped it.
  ((SECONDS - started < 10)) || false
  ! kill -0 "${claude}" 2>/dev/null || false
  [[ ! -e "$(dirname "${socket}")" ]] || false
}
