#!/bin/bash
#
# Builds face_landmarker_fixtures and libfacepalm_mediapipe.dylib, the shim
# facepalm runs the face model through on macOS, from MediaPipe's own C++
# source, pinned to a release, and fetches MediaPipe's test model and
# portrait image. Everything lands in build-mediapipe/, which git ignores.
#
# On a Mac whose Xcode cannot read its SDK, set DEVELOPER_DIR, for example to
# /Library/Developer/CommandLineTools; it is passed through to Bazel.
# BAZEL_DISK_CACHE, when set, names a folder Bazel keeps its compiled outputs
# in between runs, as CI does.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly HERE="${ROOT}/tools/fixtures"
readonly WORK="${ROOT}/build-mediapipe"
readonly MEDIAPIPE="${WORK}/mediapipe"
readonly TAG="v0.10.35"
readonly COMMIT="f8ef212d5c962c0e853db7e59d217056b187084b"
readonly ASSETS_HOST="https://storage.googleapis.com"
readonly ASSETS="${ASSETS_HOST}/mediapipe-assets/tasks/testdata/vision"
readonly BAZEL="${BAZEL:-bazelisk}"

#######################################
# Clones MediaPipe at the pinned commit, patches it, and links the tool, the
# shim and facepalm's sources in.
#######################################
checkout() {
  if [[ ! -d "${MEDIAPIPE}" ]]; then
    git clone --quiet --depth 1 --branch "${TAG}" \
      https://github.com/google-ai-edge/mediapipe.git "${MEDIAPIPE}"
    git -C "${MEDIAPIPE}" apply "${HERE}/opencv.diff"
  fi
  local head
  head="$(git -C "${MEDIAPIPE}" rev-parse HEAD)"
  if [[ "${head}" != "${COMMIT}" ]]; then
    echo "MediaPipe is at ${head}, not ${COMMIT}" >&2
    return 1
  fi
  ln -sfn "${HERE}" "${MEDIAPIPE}/facepalm_fixtures"
  ln -sfn "${ROOT}/src/mediapipe_shim" "${MEDIAPIPE}/facepalm_shim"
  ln -sfn "${ROOT}/src" "${MEDIAPIPE}/src"
}

#######################################
# Downloads one of MediaPipe's test assets and checks its SHA-256, as
# MediaPipe's third_party/external_files.bzl pins it.
# Arguments:
#   Asset name, its generation, its SHA-256.
#######################################
fetch() {
  local file="${WORK}/data/$1"
  mkdir -p "${WORK}/data"
  curl --silent --show-error --fail --output "${file}" \
    "${ASSETS}/$1?generation=$2"
  shasum -a 256 -c - <<<"$3  ${file}" >/dev/null
}

main() {
  local system
  system="$(uname -s)"
  if [[ "${system}" == Linux ]]; then
    echo 'Linux links libmediapipe.so: see docs/linux-camera.md' >&2
    return 1
  fi
  if [[ "${system}" != Darwin ]]; then
    echo "unsupported operating system" >&2
    return 1
  fi
  checkout
  fetch face_landmarker_v2_with_blendshapes.task 1782184688504388 \
    b261925d4aad812b47a0e8d58c1baa1223270a5d1f663d78338bc881c003879d
  fetch portrait.jpg 1782185108020964 \
    a6f11efaa834706db23f275b6115058fa87fc7f14362681e6abe14e82749de3e
  fetch face_detection_short_range.tflite 1782184645070605 \
    bbff11cebd1eb27a1e004cae0b0e63ec8c551cbf34a4451148b4908b8db3eca8
  local env_flags=()
  if [[ -n "${DEVELOPER_DIR:-}" ]]; then
    local flag
    for flag in repo_env action_env host_action_env; do
      env_flags+=("--${flag}=DEVELOPER_DIR=${DEVELOPER_DIR}")
    done
  fi
  if [[ -n "${BAZEL_DISK_CACHE:-}" ]]; then
    env_flags+=("--disk_cache=${BAZEL_DISK_CACHE}")
  fi
  cd "${MEDIAPIPE}"
  "${BAZEL}" --bazelrc="${HERE}/mediapipe.bazelrc" build \
    ${env_flags[@]+"${env_flags[@]}"} \
    //facepalm_fixtures:face_landmarker_fixtures \
    //facepalm_shim:libfacepalm_mediapipe.dylib
}

main "$@"
