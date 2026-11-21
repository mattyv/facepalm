#!/bin/bash
# Waits for the X display xvfb-run set in DISPLAY to accept connections,
# then runs the command given as arguments. xvfb-run's own readiness check
# only waits for the server's lock file to appear, which can happen before
# the server itself accepts clients, so a command started right after
# xvfb-run can still fail to connect to the display (#101).
#
# Usage: wait_for_display.sh COMMAND [ARG...]
set -euo pipefail

readonly DEFAULT_MAX_ATTEMPTS=20
readonly DEFAULT_POLL_SECONDS=1

fail() {
  echo "$*" >&2
  exit 1
}

# Polls xdpyinfo against DISPLAY until it succeeds, or fails once the
# attempts run out.
# Globals: DISPLAY, WAIT_FOR_DISPLAY_MAX_ATTEMPTS, WAIT_FOR_DISPLAY_POLL_SECONDS
wait_for_display() {
  local max_attempts="${WAIT_FOR_DISPLAY_MAX_ATTEMPTS:-${DEFAULT_MAX_ATTEMPTS}}"
  local poll_seconds="${WAIT_FOR_DISPLAY_POLL_SECONDS:-${DEFAULT_POLL_SECONDS}}"
  local attempt
  for ((attempt = 0; attempt < max_attempts; attempt++)); do
    if xdpyinfo -display "${DISPLAY}" >/dev/null 2>&1; then
      return 0
    fi
    sleep "${poll_seconds}"
  done
  fail "display ${DISPLAY} did not accept connections in time"
}

main() {
  if (($# == 0)); then
    echo "usage: wait_for_display.sh COMMAND [ARG...]" >&2
    exit 2
  fi
  if [[ -z "${DISPLAY:-}" ]]; then
    fail "DISPLAY is not set"
  fi
  wait_for_display
  exec "$@"
}

main "$@"
