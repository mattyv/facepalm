#!/bin/bash
# Fails when an app input links to the build machine's own libraries, the
# daemon does not find the shim in the app's Frameworks, or Node links the
# shim. Called by build_app.sh before it stages anything.
set -euo pipefail

readonly SHIM_LINK='@rpath/libfacepalm_mediapipe.dylib'

fail() {
  echo "$*" >&2
  exit 1
}

main() {
  if (($# != 4)); then
    echo "usage: check_app_links.sh MENU DAEMON SHIM NODE" >&2
    exit 2
  fi
  local binary
  for binary in "$@"; do
    otool -L "${binary}" | awk -v shim="${SHIM_LINK}" '
      NR > 1 && $1 !~ /^\/usr\/lib\// && $1 !~ /^\/System\/Library\// &&
        $1 != shim { exit 1 }' \
      || fail "non-relocatable dependency in ${binary}"
  done
  if ! otool -L "$2" | grep -Fq "${SHIM_LINK}" || ! otool -l "$2" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { need = 1; next }
    need && $1 == "path" {
      if ($2 != "@executable_path/../Frameworks") exit 1
      need = 0; found = 1
    }
    END { if (!found || need) exit 1 }'; then
    fail "daemon must link the shim through the app's Frameworks directory"
  fi
  if otool -L "$4" | grep -Fq "${SHIM_LINK}"; then
    fail "unexpected shim dependency in Node"
  fi
}

main "$@"
