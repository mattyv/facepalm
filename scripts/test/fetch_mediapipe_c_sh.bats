#!/usr/bin/env bats
#
# Tests scripts/fetch_mediapipe_c.sh against a fake curl and unzip, and a
# checksum list of its own, without network access.

setup() {
  WORK="$(mktemp -d)"
  mkdir -p "${WORK}/scripts" "${WORK}/bin"
  cp "${BATS_TEST_DIRNAME}/../fetch_mediapipe_c.sh" "${WORK}/scripts/"
  export CALLS="${WORK}/calls" HEADER=header
  OUT="${WORK}/out"
  # Logs each URL, and writes HEADER for a header and "wheel" for the wheel,
  # or fails when HEADER is "fail".
  cat >"${WORK}/bin/curl" <<'EOF'
#!/bin/bash
while (($#)); do
  if [[ "$1" == '-o' ]]; then
    out="$2"
    shift
  fi
  url="$1"
  shift
done
echo "${url}" >>"${CALLS}"
content=wheel
if [[ "${url}" == https://raw.githubusercontent.com/* ]]; then
  content="${HEADER}"
fi
[[ "${content}" == fail ]] && exit 22
printf '%s' "${content}" >"${out}"
EOF
  # Logs its arguments, and writes "library" into the folder after -d.
  cat >"${WORK}/bin/unzip" <<'EOF'
#!/bin/bash
echo "unzip $*" >>"${CALLS}"
while [[ "$1" != '-d' ]]; do shift; done
printf library >"$2/libmediapipe.so"
EOF
  chmod +x "${WORK}"/bin/*
  PATH="${WORK}/bin:${PATH}"
  local header wheel library
  header="$(printf header | shasum -a 256)"
  wheel="$(printf wheel | shasum -a 256)"
  library="$(printf library | shasum -a 256)"
  printf '%s  %s\n' "${header%% *}" include/mediapipe/tasks/c/core/common.h \
    "${wheel%% *}" mediapipe.whl "${library%% *}" lib/libmediapipe.so \
    >"${WORK}/scripts/mediapipe_c.sha256"
}

teardown() {
  rm -rf "${WORK}"
}

fetch() {
  run bash "${WORK}/scripts/fetch_mediapipe_c.sh" "${OUT}"
}

@test "fetches each file to its place and checks it" {
  fetch
  [[ "${status}" -eq 0 ]] || false
  [[ "$(<"${OUT}/include/mediapipe/tasks/c/core/common.h")" == header ]] \
    || false
  [[ "$(<"${OUT}/lib/libmediapipe.so")" == library ]] || false
  local commit=f8ef212d5c962c0e853db7e59d217056b187084b
  [[ "$(<"${CALLS}")" == *"/mediapipe/${commit}/mediapipe/tasks/c/core/"* ]] \
    || false
  [[ "$(<"${CALLS}")" == *'mediapipe-0.10.35-py3-none-manylinux_2_28'* ]] \
    || false
  [[ "$(<"${CALLS}")" == *'mediapipe/tasks/c/libmediapipe.so'* ]] || false
}

@test "keeps the files already there with their hashes" {
  fetch
  rm "${CALLS}"
  fetch
  [[ "${status}" -eq 0 ]] || false
  [[ ! -e "${CALLS}" ]] || false
}

@test "fetches again a file whose hash is wrong" {
  fetch
  printf stale >"${OUT}/include/mediapipe/tasks/c/core/common.h"
  rm "${CALLS}"
  fetch
  [[ "${status}" -eq 0 ]] || false
  local calls
  calls="$(wc -l <"${CALLS}")"
  ((calls == 1)) || false
  [[ "$(<"${OUT}/include/mediapipe/tasks/c/core/common.h")" == header ]] \
    || false
}

@test "fails when a download does not have its hash" {
  HEADER=tampered
  fetch
  [[ "${status}" -ne 0 ]] || false
}

@test "fails when a download fails" {
  HEADER=fail
  fetch
  [[ "${status}" -ne 0 ]] || false
}
