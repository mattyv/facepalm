#!/bin/bash
# Downloads and verifies the pinned MediaPipe model for the Ubuntu package.
set -euo pipefail

readonly host='https://storage.googleapis.com/mediapipe-assets'
readonly asset='tasks/testdata/vision/face_landmarker_v2_with_blendshapes.task'
readonly url="${host}/${asset}?generation=1782184688504388"
readonly hash='b261925d4aad812b47a0e8d58c1baa1223270a5d1f663d78338bc881c003879d'
prefix=/usr
if [[ $# -gt 0 ]]; then
  if [[ $# -ne 2 || "$1" != --prefix || -z "$2" ]]; then
    echo "Usage: $0 [--prefix PATH]" >&2
    exit 2
  fi
  prefix="$2"
fi

work="$(mktemp -d)"
staged=''
cleanup() {
  if [[ -n "${staged}" ]]; then
    rm -f "${staged}"
  fi
  rm -rf "${work}"
}
trap cleanup EXIT
curl --silent --show-error --fail --location --output "${work}/model" "${url}"
printf '%s  %s\n' "${hash}" "${work}/model" | sha256sum --check --status
model_dir="${prefix}/share/facepalm/model"
install -d -m 0755 "${model_dir}"
staged="$(mktemp "${model_dir}/.facepalm-model.XXXXXXXX")"
install -m 0644 "${work}/model" "${staged}"
mv -fT "${staged}" "${model_dir}/face_landmarker.task"
staged=''
echo "Installed face landmarker in ${model_dir}/face_landmarker.task"
