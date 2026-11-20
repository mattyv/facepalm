#!/bin/bash
# Run Claude with one private Facepalm hook endpoint for this session.
set -euo pipefail
PLUGIN="$(cd "$(dirname "$0")" && pwd)"
readonly PLUGIN
NODE="${FACEPALM_NODE:-$(command -v node)}"
readonly NODE
if [[ ! -x "${NODE}" ]]; then
  echo "Set FACEPALM_NODE to an executable Node 22 or newer" >&2
  exit 1
fi
DIRECTORY="$(mktemp -d "${TMPDIR:-/tmp}/fph.XXXXXX")"
readonly DIRECTORY
child=""
# Invoked by the EXIT trap.
# shellcheck disable=SC2329
cleanup() {
  if [[ -n "${child}" ]] && kill -0 "${child}" 2>/dev/null; then
    kill -TERM "${child}" 2>/dev/null || true
    wait "${child}" 2>/dev/null || true
  fi
  rm -rf "${DIRECTORY}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
export FACEPALM_HOOK_SOCKET="${DIRECTORY}/s"
PATH="$(dirname "${NODE}"):${PATH}"
export PATH
claude --plugin-dir "${PLUGIN}" "$@" <&0 &
child=$!
if wait "${child}"; then
  result=0
else
  result=$?
fi
child=""
exit "${result}"
