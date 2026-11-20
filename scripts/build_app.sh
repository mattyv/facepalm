#!/bin/bash
# Stages a local, unsigned app from already-built inputs; never installs it.
set -euo pipefail
if [[ "$#" -ne 6 ]]; then
  echo "usage: build_app.sh MENU DAEMON SHIM MODEL NODE_DIR OUTPUT.app" >&2
  exit 2
fi
readonly MENU="$1" DAEMON="$2" SHIM="$3" MODEL="$4" OUTPUT="$6"
NODE_DIR="$(cd "$5" && pwd)"
readonly NODE_DIR
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
readonly ROOT
for file in "${MENU}" "${DAEMON}" "${SHIM}" "${MODEL}" \
  "${NODE_DIR}/bin/node" "${NODE_DIR}/LICENSE" "${ROOT}/mcp/main.js" \
  "${ROOT}/mcp/package.json" "${ROOT}/mcp/server.js" \
  "${ROOT}/mcp/session.js" "${ROOT}/mcp/hook-broker.js" \
  "${ROOT}/integrations/claude-code/.claude-plugin/plugin.json" \
  "${ROOT}/scripts/facepalm-info.plist" \
  "${ROOT}/LICENSE" "${ROOT}/THIRDPARTY.md"; do
  if [[ ! -f "${file}" ]]; then
    echo "missing input: ${file}" >&2
    exit 1
  fi
done
if [[ ! -x "${MENU}" ]] || [[ ! -x "${DAEMON}" ]] || [[ ! -x "${NODE_DIR}/bin/node" ]]; then
  echo "menu, daemon and Node must be executable" >&2
  exit 1
fi
for directory in "${ROOT}/mcp/node_modules/@modelcontextprotocol/sdk" \
  "${ROOT}/mcp/node_modules/zod"; do
  if [[ ! -d "${directory}" ]]; then
    echo "missing dependency: ${directory}; run npm ci in mcp" >&2
    exit 1
  fi
done
if [[ -e "${OUTPUT}" ]] || [[ -L "${OUTPUT}" ]]; then
  echo "output already exists: ${OUTPUT}" >&2
  exit 1
fi
version="$("${NODE_DIR}/bin/node" --version)"
major="${version#v}"
major="${major%%.*}"
if ! [[ "${major}" =~ ^[0-9]+$ ]] || [[ "${major}" -lt 22 ]]; then
  echo "Node 22 or newer is required" >&2
  exit 1
fi
# Reject dependencies on the build machine's Homebrew or other local paths.
for binary in "${MENU}" "${DAEMON}" "${SHIM}" "${NODE_DIR}/bin/node"; do
  dependencies="$(otool -L "${binary}")"
  if ! printf '%s\n' "${dependencies}" | awk '
    NR > 1 && $1 !~ /^\/usr\/lib\// && $1 !~ /^\/System\/Library\// &&
      $1 != "@rpath/libfacepalm_mediapipe.dylib" { exit 1 }
  '; then
    echo "non-relocatable dependency in ${binary}" >&2
    exit 1
  fi
done
if ! otool -L "${DAEMON}" | grep -Fq '@rpath/libfacepalm_mediapipe.dylib' \
  || ! otool -l "${DAEMON}" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { need = 1; next }
    need && $1 == "path" {
      if ($2 != "@executable_path/../Frameworks") exit 1
      need = 0; found = 1
    }
    END { if (!found || need) exit 1 }
  '; then
  echo "daemon must link the shim through the app's Frameworks directory" >&2
  exit 1
fi
if otool -L "${NODE_DIR}/bin/node" | grep -Fq '@rpath/libfacepalm_mediapipe.dylib'; then
  echo "unexpected shim dependency in Node" >&2
  exit 1
fi
if ! (cd "${ROOT}/mcp" && "${NODE_DIR}/bin/node" --input-type=module -e \
  'await import("./server.js"); await import("./session.js")'); then
  echo "the Node runtime cannot load the adapter dependencies" >&2
  exit 1
fi
STAGING="$(mktemp -d "${OUTPUT}.XXXXXX")"
readonly STAGING
trap 'rm -rf "${STAGING}"' EXIT
mkdir -p "${STAGING}/Contents/MacOS" "${STAGING}/Contents/Frameworks" \
  "${STAGING}/Contents/Resources/model" "${STAGING}/Contents/Resources/mcp" \
  "${STAGING}/Contents/Resources/node/bin"
cp "${MENU}" "${STAGING}/Contents/MacOS/facepalm-menu"
cp "${DAEMON}" "${STAGING}/Contents/MacOS/facepalm-daemon"
cp "${SHIM}" "${STAGING}/Contents/Frameworks/libfacepalm_mediapipe.dylib"
cp "${MODEL}" "${STAGING}/Contents/Resources/model/face_landmarker.task"
cp "${NODE_DIR}/bin/node" "${STAGING}/Contents/Resources/node/bin/node"
cp "${NODE_DIR}/LICENSE" "${STAGING}/Contents/Resources/node/LICENSE"
cp "${ROOT}/mcp/main.js" "${ROOT}/mcp/server.js" \
  "${ROOT}/mcp/session.js" "${ROOT}/mcp/hook-broker.js" \
  "${ROOT}/mcp/package.json" \
  "${STAGING}/Contents/Resources/mcp/"
cp -R "${ROOT}/mcp/node_modules" "${STAGING}/Contents/Resources/mcp/"
mkdir -p "${STAGING}/Contents/Resources/claude-code"
for component in .claude-plugin hooks lib skills facepalm-claude.sh README.md; do
  cp -R "${ROOT}/integrations/claude-code/${component}" \
    "${STAGING}/Contents/Resources/claude-code/"
done
cp "${ROOT}/LICENSE" "${ROOT}/THIRDPARTY.md" "${STAGING}/Contents/Resources/"
cp "${ROOT}/scripts/facepalm-info.plist" "${STAGING}/Contents/Info.plist"
mkdir "${OUTPUT}"
trap 'rm -rf "${STAGING}" "${OUTPUT}"' EXIT
cp -R "${STAGING}/." "${OUTPUT}"
trap 'rm -rf "${STAGING}"' EXIT
