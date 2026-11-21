#!/usr/bin/env bats
#
# Tests scripts/coverage_shell.sh against fake kcov and bats on the PATH, in
# a small git repository of its own.

setup() {
  WORK="$(mktemp -d)"
  REPO="${WORK}/repo"
  mkdir -p "${REPO}/scripts" "${REPO}/dir" "${REPO}/test" "${WORK}/bin"
  cp "${BATS_TEST_DIRNAME}/../coverage_shell.sh" "${REPO}/scripts/"
  git -C "${REPO}" init --quiet
  track a.sh test/a_sh.bats
  # Stands in for kcov: logs the command it traces, writes the report the
  # test set up, and runs the command.
  cat >"${WORK}/bin/kcov" <<'EOF'
#!/bin/bash
while [[ "$1" == --* ]]; do
  shift
done
mkdir -p "$1/bats"
cp "${FAKE_REPORT}" "$1/bats/cobertura.xml"
shift
echo "$@" >>"${FAKE_LOG}"
"$@"
EOF
  cat >"${WORK}/bin/bats" <<'EOF'
#!/bin/bash
exit "${FAKE_BATS_STATUS:-0}"
EOF
  chmod +x "${WORK}"/bin/*
  PATH="${WORK}/bin:${PATH}"
  export FAKE_REPORT="${WORK}/cobertura.xml" FAKE_LOG="${WORK}/kcov.log"
  floors ''
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

# Writes coverage_thresholds.json with the given entries.
floors() {
  printf '{%s}\n' "$1" >"${REPO}/coverage_thresholds.json"
}

# Writes the fake kcov's Cobertura report: a source, then for each class its
# filename and its lines as "number:hits".
report() {
  local source="$1" lines line
  shift
  {
    printf '<coverage>\n<sources><source>%s</source></sources>\n' "${source}"
    while (($# > 0)); do
      printf '<class name="x" filename="%s" line-rate="0">\n<lines>\n' "$1"
      read -r -a lines <<<"$2"
      for line in "${lines[@]}"; do
        printf '<line number="%s" hits="%s"/>\n' "${line%:*}" "${line#*:}"
      done
      printf '</lines>\n</class>\n'
      shift 2
    done
    printf '</coverage>\n'
  } >"${FAKE_REPORT}"
}

@test "passes when every script's lines are covered, and reports them" {
  report / "${REPO}/a.sh" '2:1 3:4'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 0 ]] || false
  [[ "${output}" == *'a.sh lines 100.00% (2 of 2)'* ]] || false
}

@test "traces each bats file in git under kcov" {
  track test/b_sh.bats
  report / "${REPO}/a.sh" '2:1'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 0 ]] || false
  local log
  log="$(cat "${FAKE_LOG}")"
  [[ "${log}" == $'bats test/a_sh.bats\nbats test/b_sh.bats' ]] || false
}

@test "fails, naming the script, when its lines are below its floor" {
  report / "${REPO}/a.sh" '2:1 3:0'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'a.sh: lines 50.00% is below 100%'* ]] || false
}

@test "holds a script to the floor listed for it" {
  floors '"a.sh": {"shell": {"lines": 50}}'
  report / "${REPO}/a.sh" '2:1 3:0'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 0 ]] || false
}

@test "counts a line covered in any copy of the script" {
  report / /tmp/one/a.sh '2:1 3:0' /tmp/two/a.sh '2:0 3:1'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 0 ]] || false
  [[ "${output}" == *'a.sh lines 100.00% (2 of 2)'* ]] || false
}

@test "reads a filename relative to the report's source" {
  track dir/c.sh
  report /tmp/one/dir c.sh '2:1'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'dir/c.sh lines 100.00% (1 of 1)'* ]] || false
  [[ "${output}" == *'a.sh: no test ran it'* ]] || false
}

@test "credits the script whose path matches the longest" {
  track dir/a.sh
  report / /tmp/one/dir/a.sh '2:1'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'dir/a.sh lines 100.00% (1 of 1)'* ]] || false
  [[ "${output}" == *$'\na.sh: no test ran it'* ]] || false
}

@test "fails when a test fails" {
  report / "${REPO}/a.sh" '2:1'
  FAKE_BATS_STATUS=1 run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -ne 0 ]] || false
}

@test "starts each run from no earlier report" {
  mkdir -p "${REPO}/build-shell-coverage/old/bats"
  report / "${REPO}/a.sh" '2:1 3:0'
  cp "${FAKE_REPORT}" "${REPO}/build-shell-coverage/old/bats/cobertura.xml"
  report / "${REPO}/a.sh" '2:1'
  run bash "${REPO}/scripts/coverage_shell.sh"
  [[ "${status}" -eq 0 ]] || false
}
