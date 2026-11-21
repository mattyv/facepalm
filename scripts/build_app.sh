#!/bin/bash
# Stages a local, unsigned app from already-built inputs; never installs it.
set -euo pipefail

MCP_FILES=(main.js package.json server.js session.js hook-broker.js)
MCP_FILES+=(settings-launcher.js)
MCP_FILES+=(feeling-widget.js feeling-widget.html feeling-elicitation.js)
readonly MCP_FILES

fail() {
  echo "$*" >&2
  exit 1
}

# Fails unless every input exists, OUTPUT does not, and Node is 22 or newer.
check_inputs() {
  local file major
  for file in "${MENU}" "${DAEMON}" "${SHIM}" "${MODEL}" \
    "${NODE_DIR}"/{bin/node,LICENSE} "${MCP_FILES[@]/#/${ROOT}/mcp/}" \
    "${ROOT}/integrations/claude-code/.claude-plugin/plugin.json" \
    "${ROOT}"/{scripts/facepalm-info.plist,LICENSE,THIRDPARTY.md}; do
    [[ -f "${file}" ]] || fail "missing input: ${file}"
  done
  [[ -x "${MENU}" && -x "${DAEMON}" && -x "${NODE_DIR}/bin/node" ]] \
    || fail "menu, daemon and Node must be executable"
  for file in "${ROOT}"/mcp/node_modules/{@modelcontextprotocol/sdk,zod}; do
    [[ -d "${file}" ]] || fail "missing dependency: ${file}; run npm ci in mcp"
  done
  [[ ! -e "${OUTPUT}" && ! -L "${OUTPUT}" ]] \
    || fail "output already exists: ${OUTPUT}"
  major="$("${NODE_DIR}/bin/node" --version)"
  major="${major#v}" && major="${major%%.*}"
  [[ "${major}" =~ ^[0-9]+$ ]] || major=0
  ((major >= 22)) || fail "Node 22 or newer is required"
}

# Fails on a non-relocatable link, or a Node that cannot load the adapter.
check_links() {
  bash "${ROOT}/scripts/check_app_links.sh" "${MENU}" "${DAEMON}" "${SHIM}" \
    "${NODE_DIR}/bin/node"
  (cd "${ROOT}/mcp" && "${NODE_DIR}/bin/node" --input-type=module -e \
    'await import("./server.js"); await import("./session.js")') \
    || fail "the Node runtime cannot load the adapter dependencies"
}

# Copies the inputs into STAGING, then STAGING into OUTPUT.
stage() {
  local contents component
  STAGING="$(mktemp -d "${OUTPUT}.XXXXXX")"
  trap 'rm -rf "${STAGING}"' EXIT
  contents="${STAGING}/Contents"
  mkdir -p "${contents}"/{MacOS,Frameworks} \
    "${contents}/Resources"/{model,mcp,node/bin,claude-code}
  cp "${MENU}" "${contents}/MacOS/facepalm-menu"
  cp "${DAEMON}" "${contents}/MacOS/facepalm-daemon"
  cp "${SHIM}" "${contents}/Frameworks/libfacepalm_mediapipe.dylib"
  cp "${MODEL}" "${contents}/Resources/model/face_landmarker.task"
  cp "${NODE_DIR}/bin/node" "${contents}/Resources/node/bin/node"
  cp "${NODE_DIR}/LICENSE" "${contents}/Resources/node/LICENSE"
  cp "${MCP_FILES[@]/#/${ROOT}/mcp/}" "${contents}/Resources/mcp/"
  cp -R "${ROOT}/mcp/node_modules" "${contents}/Resources/mcp/"
  for component in .claude-plugin hooks lib skills facepalm-claude.sh \
    README.md; do
    cp -R "${ROOT}/integrations/claude-code/${component}" \
      "${contents}/Resources/claude-code/"
  done
  cp "${ROOT}/LICENSE" "${ROOT}/THIRDPARTY.md" "${contents}/Resources/"
  cp "${ROOT}/scripts/facepalm-info.plist" "${contents}/Info.plist"
  mkdir "${OUTPUT}"
  trap 'rm -rf "${STAGING}" "${OUTPUT}"' EXIT
  cp -R "${STAGING}/." "${OUTPUT}"
  trap 'rm -rf "${STAGING}"' EXIT
}

main() {
  if (($# != 6)); then
    echo "usage: build_app.sh MENU DAEMON SHIM MODEL NODE_DIR OUTPUT.app" >&2
    exit 2
  fi
  readonly MENU="$1" DAEMON="$2" SHIM="$3" MODEL="$4" OUTPUT="$6"
  NODE_DIR="$(cd "$5" && pwd)"
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
  check_inputs
  check_links
  stage
}

main "$@"
