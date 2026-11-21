#!/usr/bin/env bats
#
# Tests scripts/coverage_js.sh against a fake node on the PATH, in a small
# git repository of its own.

setup() {
  WORK="$(mktemp -d)"
  REPO="${WORK}/repo"
  mkdir -p "${REPO}/scripts" "${REPO}/mcp/test" "${WORK}/bin"
  cp "${BATS_TEST_DIRNAME}/../coverage_js.sh" "${REPO}/scripts/"
  git -C "${REPO}" init --quiet
  track mcp/a.js mcp/test/a.test.js
  # Stands in for node: writes the LCOV content the test set up at whatever
  # --test-reporter-destination follows --test-reporter=lcov, logs the
  # source this call covers, and exits with the status the test wants.
  cat >"${WORK}/bin/node" <<'EOF'
#!/bin/bash
include=''
lcov_dest=''
want_lcov=0
for arg in "$@"; do
  case "${arg}" in
    --test-coverage-include=*) include="${arg#*=}" ;;
    --test-reporter=lcov) want_lcov=1 ;;
    --test-reporter-destination=*)
      if [[ "${want_lcov}" -eq 1 && -z "${lcov_dest}" ]]; then
        lcov_dest="${arg#*=}"
      fi
      want_lcov=0
      ;;
  esac
done
echo "${include}" >>"${FAKE_LOG}"
if [[ -n "${lcov_dest}" ]]; then
  key="FAKE_LCOV_$(printf '%s' "${include}" | tr -c 'A-Za-z0-9' '_')"
  content="${!key:-${FAKE_LCOV_DEFAULT:-}}"
  printf '%s' "${content}" >"${lcov_dest}"
fi
exit "${FAKE_NODE_STATUS:-0}"
EOF
  chmod +x "${WORK}/bin/node"
  PATH="${WORK}/bin:${PATH}"
  export FAKE_LOG="${WORK}/node.log"
}

teardown() {
  rm -rf "${WORK}"
}

# Adds empty files to the repository's index.
track() {
  local file
  for file in "$@"; do
    mkdir -p "$(dirname "${REPO}/${file}")"
    touch "${REPO}/${file}"
  done
  git -C "${REPO}" add "$@"
}

# Sets FAKE_LCOV_DEFAULT to a full-coverage LCOV record for mcp/a.js, the
# content the fake node writes for whichever source it is asked to cover,
# when nothing more specific is set for that exact source.
set_full_record() {
  read -r -d '' FAKE_LCOV_DEFAULT <<'EOF' || true
SF:mcp/a.js
FN:1,f
FNDA:1,f
DA:1,1
BRDA:1,0,0,1
end_of_record
EOF
  export FAKE_LCOV_DEFAULT
}

@test "passes when the source's own test file covers it fully" {
  set_full_record
  run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 0 ]] || false
  local log
  log="$(cat "${FAKE_LOG}")"
  [[ "${log}" == 'mcp/a.js' ]] || false
}

@test "fails, naming the file, when it has no test file of its own" {
  track mcp/b.js
  set_full_record
  run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'mcp/b.js: has no test file of its own (mcp/test/b.test.js)'* ]] || false
}

@test "fails, naming it, when a function, line or branch is short of all of them" {
  read -r -d '' FAKE_LCOV_DEFAULT <<'EOF' || true
SF:mcp/a.js
FN:1,f
FN:2,g
FNDA:1,f
FNDA:0,g
DA:1,1
DA:2,0
BRDA:1,0,0,1
BRDA:2,1,0,0
end_of_record
EOF
  export FAKE_LCOV_DEFAULT
  run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'mcp/a.js: FN 1 of 2'* ]] || false
  [[ "${output}" == *'mcp/a.js: L 1 of 2'* ]] || false
  [[ "${output}" == *'mcp/a.js: BR 1 of 2'* ]] || false
}

@test "fails when the source's own test did not pass" {
  set_full_record
  FAKE_NODE_STATUS=1 run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'mcp/a.js: its own test did not pass'* ]] || false
}

@test "fails when the test run never loaded the source" {
  read -r -d '' FAKE_LCOV_DEFAULT <<'EOF' || true
SF:mcp/other.js
FN:1,f
FNDA:1,f
end_of_record
EOF
  export FAKE_LCOV_DEFAULT
  run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'mcp/a.js: its test run never loaded it'* ]] || false
}

@test "merges more than one record for the same file, as a re-import under a cache-busting query gives" {
  read -r -d '' FAKE_LCOV_DEFAULT <<'EOF' || true
SF:mcp/a.js
FN:1,f
FNDA:0,f
DA:1,0
BRDA:1,0,0,0
end_of_record
SF:mcp/a.js
FN:1,f
FNDA:1,f
DA:1,1
BRDA:1,0,0,1
end_of_record
EOF
  export FAKE_LCOV_DEFAULT
  run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 0 ]] || false
}

@test "discovers a file under mcp/widget/ and under integrations/claude-code/" {
  track mcp/widget/w.js mcp/widget/test/w.test.js \
    integrations/claude-code/hooks/h.mjs integrations/claude-code/test/h.test.mjs
  set_full_record
  read -r -d '' FAKE_LCOV_mcp_widget_w_js <<'EOF' || true
SF:mcp/widget/w.js
FN:1,f
FNDA:1,f
DA:1,1
BRDA:1,0,0,1
end_of_record
EOF
  export FAKE_LCOV_mcp_widget_w_js
  read -r -d '' FAKE_LCOV_integrations_claude_code_hooks_h_mjs <<'EOF' || true
SF:integrations/claude-code/hooks/h.mjs
FN:1,f
FNDA:1,f
DA:1,1
BRDA:1,0,0,1
end_of_record
EOF
  export FAKE_LCOV_integrations_claude_code_hooks_h_mjs
  run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 0 ]] || false
  local log
  log="$(sort "${FAKE_LOG}")"
  [[ "${log}" == $'integrations/claude-code/hooks/h.mjs\nmcp/a.js\nmcp/widget/w.js' ]] || false
}

@test "excludes a file under a test directory from needing its own test" {
  track mcp/test/helper.js
  set_full_record
  run bash "${REPO}/scripts/coverage_js.sh"
  [[ "${status}" -eq 0 ]] || false
  [[ "${output}" != *'helper.js'* ]] || false
}
