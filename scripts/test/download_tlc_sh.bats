#!/usr/bin/env bats
#
# Tests scripts/download_tlc.sh against a fake curl, without network access.
# Adapted from ai-pair's scripts/test/download_tlc_sh.bats.

# The SHA-256 of "abc".
ABC_SHA256=ba7816bf8f01cfea414140de5dae2223
ABC_SHA256+=b00361a396177a9cb410ff61f20015ad
readonly ABC_SHA256

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/bin"
  export CALLS="${WORK}/calls" PRIMARY=fail FALLBACK=fail
  OUTPUT="${WORK}/tla.jar"
  # Logs each URL and writes PRIMARY or FALLBACK, or fails when it is "fail".
  cat >"${WORK}/bin/curl" <<'EOF'
#!/bin/bash
out=''
url=''
while (($#)); do
  if [[ "$1" == '-o' ]]; then
    out="$2"
    shift 2
  else
    url="$1"
    shift
  fi
done
echo "${url}" >>"${CALLS}"
content="${FALLBACK}"
if [[ "${url}" == *github.com/tlaplus/tlaplus/releases/download/* ]]; then
  content="${PRIMARY}"
fi
[[ "${content}" == fail ]] && exit 22
printf '%s' "${content}" >"${out}"
EOF
  chmod +x "${WORK}/bin/curl"
  PATH="${WORK}/bin:${PATH}"
}

teardown() {
  rm -rf "${WORK}"
}

download() {
  run bash "${BATS_TEST_DIRNAME}/../download_tlc.sh" "${OUTPUT}" \
    "${ABC_SHA256}"
  calls="$(wc -l <"${CALLS}")"
  content=''
  if [[ -e "${OUTPUT}" ]]; then content="$(cat "${OUTPUT}")"; fi
}

@test "uses the primary when its content matches" {
  PRIMARY=abc
  download
  [[ "${status}" -eq 0 ]]
  ((calls == 1))
  [[ "${content}" == abc ]]
}

@test "falls back after a failure" {
  FALLBACK=abc
  download
  [[ "${status}" -eq 0 ]]
  ((calls == 2))
  [[ "${content}" == abc ]]
}

@test "falls back after a checksum mismatch" {
  PRIMARY=bad FALLBACK=abc
  download
  [[ "${status}" -eq 0 ]]
  ((calls == 2))
  [[ "${content}" == abc ]]
}

@test "fails and removes the output when neither source matches" {
  PRIMARY=bad FALLBACK=bad
  download
  [[ "${status}" -ne 0 ]]
  [[ ! -e "${OUTPUT}" ]]
}

@test "fails and removes the output when both downloads fail" {
  printf stale >"${OUTPUT}"
  download
  [[ "${status}" -ne 0 ]]
  [[ ! -e "${OUTPUT}" ]]
}
