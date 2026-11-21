#!/usr/bin/env bats

setup() {
  WORK="$(mktemp -d "${TMPDIR:-/tmp}/facepalm-app.XXXXXX")"
  mkdir -p "${WORK}/scripts" \
    "${WORK}/mcp/node_modules/@modelcontextprotocol/sdk" \
    "${WORK}/mcp/node_modules/zod" "${WORK}/node/bin" "${WORK}/bin"
  cp "${BATS_TEST_DIRNAME}/../build_app.sh" \
    "${BATS_TEST_DIRNAME}/../check_app_links.sh" \
    "${BATS_TEST_DIRNAME}/../facepalm-info.plist" "${WORK}/scripts/"
  for file in main.js server.js session.js hook-broker.js; do
    printf 'adapter' >"${WORK}/mcp/${file}"
  done
  plugin="${WORK}/integrations/claude-code"
  mkdir -p "${plugin}"/{.claude-plugin,hooks,lib,skills}
  printf '{}' >"${plugin}/.claude-plugin/plugin.json"
  touch "${plugin}/facepalm-claude.sh" "${plugin}/README.md"
  printf '{}' >"${WORK}/mcp/package.json"
  printf 'gpl' >"${WORK}/LICENSE"
  printf 'notices' >"${WORK}/THIRDPARTY.md"
  for name in ui daemon shim model; do
    printf '%s' "${name}" >"${WORK}/${name}"
  done
  chmod +x "${WORK}/ui" "${WORK}/daemon"
  printf '#!/bin/bash\nprintf "v22.0.0\\n"\n' >"${WORK}/node/bin/node"
  chmod +x "${WORK}/node/bin/node"
  printf 'node licence' >"${WORK}/node/LICENSE"
  cat >"${WORK}/bin/otool" <<'TOOL'
#!/bin/bash
if [[ -n "${RACE_OUTPUT:-}" ]]; then
  mkdir -p "${RACE_OUTPUT}"
  printf keep >"${RACE_OUTPUT}/marker"
fi
if [ "$1" = -l ]; then
  printf '          cmd LC_RPATH\n'
  printf '         path @executable_path/../Frameworks (offset 12)\n'
else
  printf '%s:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1)\n' "$2"
  case "$2" in
    daemon | */daemon)
      printf '\t@rpath/libfacepalm_mediapipe.dylib (compatibility version 1)\n'
      ;;
  esac
fi
TOOL
  chmod +x "${WORK}/bin/otool"
  export PATH="${WORK}/bin:${PATH}"
}

# Runs build_app.sh on the fixture's inputs, into ${WORK}/Facepalm.app.
build_app() {
  bash "${WORK}/scripts/build_app.sh" "${WORK}/ui" "${WORK}/daemon" \
    "${WORK}/shim" "${WORK}/model" "${WORK}/node" "${WORK}/Facepalm.app"
}

teardown() {
  rm -rf "${WORK}"
}

@test "stages the app without launching it" {
  local -r app="${WORK}/Facepalm.app/Contents"
  run build_app
  [[ "${status}" -eq 0 ]] || false
  [[ -x "${app}/MacOS/facepalm-menu" ]] || false
  [[ -x "${app}/MacOS/facepalm-daemon" ]] || false
  [[ -f "${app}/Frameworks/libfacepalm_mediapipe.dylib" ]] || false
  [[ -f "${app}/Resources/model/face_landmarker.task" ]] || false
  [[ -f "${app}/Resources/node/LICENSE" ]] || false
  [[ -f "${app}/Resources/mcp/main.js" ]] || false
  [[ -f "${app}/Resources/mcp/server.js" ]] || false
  [[ -f "${app}/Resources/mcp/session.js" ]] || false
  [[ -f "${app}/Resources/mcp/hook-broker.js" ]] || false
  [[ -f "${app}/Resources/claude-code/.claude-plugin/plugin.json" ]] || false
  grep -q NSCameraUsageDescription "${WORK}/Facepalm.app/Contents/Info.plist"
}

@test "rejects missing model without producing an app" {
  rm "${WORK}/model"
  run build_app
  [[ "${status}" -ne 0 ]] || false
  [[ ! -e "${WORK}/Facepalm.app" ]] || false
}

@test "refuses to overwrite an existing app" {
  mkdir "${WORK}/Facepalm.app"
  printf 'keep' >"${WORK}/Facepalm.app/marker"
  run build_app
  [[ "${status}" -ne 0 ]] || false
  run cat "${WORK}/Facepalm.app/marker"
  [[ "${output}" = keep ]] || false
}

@test "rejects node with a dependency outside macOS" {
  cat >"${WORK}/bin/otool" <<'TOOL'
#!/bin/bash
printf '%s:\n\t/opt/homebrew/lib/libnode.dylib (compatibility version 1)\n' "$2"
TOOL
  run build_app
  [[ "${status}" -ne 0 ]] || false
  [[ ! -e "${WORK}/Facepalm.app" ]] || false
}

@test "rejects a daemon without a bundle-relative library search path" {
  cat >"${WORK}/bin/otool" <<'TOOL'
#!/bin/bash
printf '%s:\n' "$2"
TOOL
  run build_app
  [[ "${status}" -ne 0 ]] || false
  [[ ! -e "${WORK}/Facepalm.app" ]] || false
}

@test "rejects a runtime that cannot load the adapter dependencies" {
  cat >"${WORK}/node/bin/node" <<'TOOL'
#!/bin/bash
if [[ "$1" = --version ]]; then
  printf 'v22.0.0\n'
  exit 0
fi
exit 1
TOOL
  run build_app
  [[ "${status}" -ne 0 ]] || false
  [[ ! -e "${WORK}/Facepalm.app" ]] || false
}

@test "rejects a daemon that cannot be executed" {
  chmod -x "${WORK}/daemon"
  run build_app
  [[ "${status}" -ne 0 ]] || false
  [[ ! -e "${WORK}/Facepalm.app" ]] || false
}

@test "does not overwrite output created while building" {
  export RACE_OUTPUT="${WORK}/Facepalm.app"
  run build_app
  [[ "${status}" -ne 0 ]] || false
  run cat "${WORK}/Facepalm.app/marker"
  [[ "${output}" = keep ]] || false
  [[ ! -e "${WORK}/Facepalm.app/Contents" ]] || false
}

@test "rejects a daemon that searches elsewhere before its bundle path" {
  sed -i.bak 's|cmd LC_RPATH|&\\n path /tmp/other (offset 12)\\n cmd LC_RPATH|' \
    "${WORK}/bin/otool"
  run build_app
  [[ "${status}" -ne 0 ]] || false
  [[ ! -e "${WORK}/Facepalm.app" ]] || false
}

@test "accepts relative input and output paths" {
  cd "${WORK}"
  run bash scripts/build_app.sh ui daemon shim model node Facepalm.app
  [[ "${status}" -eq 0 ]] || false
  [[ -f Facepalm.app/Contents/MacOS/facepalm-daemon ]] || false
}

@test "rejects a missing menu bar executable" {
  rm "${WORK}/ui"
  run build_app
  [[ "${status}" -ne 0 ]] || false
  [[ "${output}" == *"missing input:"*"ui"* ]] || false
  [[ ! -e "${WORK}/Facepalm.app" ]] || false
}
