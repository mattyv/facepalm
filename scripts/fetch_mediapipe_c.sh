#!/bin/bash
#
# Fetches what the Linux camera daemon builds against, as
# docs/linux-camera.md says (#83): MediaPipe's C headers for the face
# landmarker, at the commit tools/fixtures/build.sh pins, into
# <folder>/include, and libmediapipe.so from the MediaPipe 0.10.35 Linux amd64
# wheel into <folder>/lib. Every file must have the SHA-256 that
# mediapipe_c.sha256, beside this script, gives it. A file already there with
# its hash is kept, so a cached folder is checked rather than fetched again.
# Usage: fetch_mediapipe_c.sh <folder>

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly HERE
readonly SUMS="${HERE}/mediapipe_c.sha256"
readonly COMMIT='f8ef212d5c962c0e853db7e59d217056b187084b'
RAW='https://raw.githubusercontent.com/google-ai-edge/mediapipe/'
RAW+="${COMMIT}"
readonly RAW
WHEEL='https://files.pythonhosted.org/packages/32/8f/'
WHEEL+='1bc57dbc9b7b03c8f875aac23380ec57e9002cc02fe6720045fb263f3966/'
WHEEL+='mediapipe-0.10.35-py3-none-manylinux_2_28_x86_64.whl'
readonly WHEEL
readonly CURL=(curl --fail --silent --show-error --location --retry 3)

main() {
  mkdir -p "$1"
  cd "$1"
  local sums sum path
  sums="$(<"${SUMS}")"
  while read -r sum path; do
    if shasum -a 256 --status -c - <<<"${sum}  ${path}" 2>/dev/null; then
      continue
    fi
    mkdir -p "$(dirname "${path}")"
    case "${path}" in
      include/*) "${CURL[@]}" -o "${path}" "${RAW}/${path#include/}" ;;
      mediapipe.whl) "${CURL[@]}" -o "${path}" "${WHEEL}" ;;
      *) unzip -o -q -j -d lib mediapipe.whl "mediapipe/tasks/c/${path#lib/}" ;;
    esac
  done <<<"${sums}"
  shasum -a 256 --quiet -c "${SUMS}"
}

main "$@"
