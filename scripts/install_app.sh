#!/bin/bash
# Copies a staged app without replacing an existing installation or
# launching it.
set -euo pipefail
if [[ "$#" -ne 2 ]] || [[ ! -f "$1/Contents/MacOS/facepalm-daemon" ]] \
  || [[ ! -d "$2" ]]; then
  echo "usage: install_app.sh Facepalm.app DESTINATION_DIRECTORY" >&2
  exit 2
fi
readonly TARGET="$2/Facepalm.app"
if [[ -e "${TARGET}" ]] || [[ -L "${TARGET}" ]]; then
  echo "installation already exists: ${TARGET}" >&2
  exit 1
fi
mkdir "${TARGET}"
trap 'rm -rf "${TARGET}"' EXIT
cp -R "$1/." "${TARGET}"
trap - EXIT
