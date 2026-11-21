#!/usr/bin/env bats
# Cross-link a real ELF shared object to exercise the Linux export boundary.

setup() {
  ROOT="${BATS_TEST_DIRNAME}/../../.."
  WORK="$(mktemp -d)"
  cat >"${WORK}/symbols.c" <<'C'
void fp_face_detector_create(void) {}
void fp_face_detector_destroy(void) {}
void fp_face_detector_detect(void) {}
void fp_face_model_create(void) {}
void fp_face_model_destroy(void) {}
void fp_face_model_detect(void) {}
void internal_mediapipe_symbol(void) {}
C
}

teardown() {
  rm -rf "${WORK}"
}

@test "ELF shim exposes only the six C ABI symbols" {
  os="$(uname -s)"
  target=(--target=x86_64-unknown-linux-gnu)
  if [[ "${os}" == Linux ]]; then
    ELF_LD="${ELF_LD:-ld}"
    ELF_NM="${ELF_NM:-nm}"
    target=()
  else
    if [[ -z "${ELF_LD:-}" || -z "${ELF_NM:-}" ]]; then
      skip "ELF linker not configured"
    fi
  fi
  "${ELF_CLANG:-clang}" "${target[@]}" -fPIC -c \
    "${WORK}/symbols.c" -o "${WORK}/symbols.o"
  "${ELF_LD}" -shared \
    --version-script="${ROOT}/src/mediapipe_shim/exports_linux.map" \
    -o "${WORK}/shim.so" "${WORK}/symbols.o"
  "${ELF_NM}" --dynamic --defined-only -j \
    "${WORK}/shim.so" >"${WORK}/symbols"
  LC_ALL=C sort "${WORK}/symbols" >"${WORK}/actual"
  cat >"${WORK}/expected" <<'SYMBOLS'
fp_face_detector_create
fp_face_detector_destroy
fp_face_detector_detect
fp_face_model_create
fp_face_model_destroy
fp_face_model_detect
SYMBOLS
  run diff -u "${WORK}/expected" "${WORK}/actual"
  [[ "${status}" -eq 0 ]] || false
}
