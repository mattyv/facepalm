#!/usr/bin/env bats
#
# Tests scripts/check_bats.sh against small .bats files.

setup() {
  WORK="$(mktemp -d)"
  CHECK="${BATS_TEST_DIRNAME}/../check_bats.sh"
}

teardown() {
  rm -rf "${WORK}"
}

@test "passes when every assertion ends in || false" {
  printf '@test "a" {\n  [[ 1 -eq 1 ]] || false\n  ((1 == 1)) || false\n}\n' \
    >"${WORK}/good.bats"
  run bash "${CHECK}" "${WORK}/good.bats"
  [[ "${status}" -eq 0 ]] || false
  [[ -z "${output}" ]] || false
}

@test "fails, naming the line, on a bare [[ ]]" {
  printf '@test "a" {\n  [[ 1 -eq 1 ]]\n  true\n}\n' >"${WORK}/bad.bats"
  run bash "${CHECK}" "${WORK}/bad.bats"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'bad.bats:2: end the assertion with || false'* ]] || false
}

@test "fails on a bare (( ))" {
  printf '@test "a" {\n  ((1 == 1))\n}\n' >"${WORK}/bad.bats"
  run bash "${CHECK}" "${WORK}/bad.bats"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'bad.bats:2:'* ]] || false
}

@test "checks every .bats file in git by default" {
  git -C "${WORK}" init --quiet
  printf '@test "a" {\n  [[ 1 -eq 1 ]]\n}\n' >"${WORK}/bad.bats"
  cd "${WORK}"
  run bash "${CHECK}"
  [[ "${status}" -eq 1 ]] || false
  [[ "${output}" == *'bad.bats:2:'* ]] || false
}

@test "passes with no .bats files" {
  git -C "${WORK}" init --quiet
  cd "${WORK}"
  run bash "${CHECK}"
  [[ "${status}" -eq 0 ]] || false
}
