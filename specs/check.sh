#!/bin/bash
#
# Checks every TLA+ config in specs/ with TLC. A config passes only when TLC
# exits 0 and reports no error, as in ai-pair's specs/check.sh; Foo_Bar.cfg
# checks Foo.tla. TLC is the command that runs TLC, by default the jar that
# scripts/download_tlc.sh puts in build-tla/.

set -euo pipefail

cd "$(dirname "$0")"
read -r -a TLC_COMMAND <<<"${TLC:-java -cp ../build-tla/tla2tools.jar tlc2.TLC}"
readonly TLC_COMMAND

main() {
  local meta status=0 cfg name out code
  meta="$(mktemp -d)"
  for cfg in ./*.cfg; do
    name="${cfg#./}"
    name="${name%.cfg}"
    code=0
    out="$("${TLC_COMMAND[@]}" -workers auto -cleanup -metadir "${meta}" \
      -config "${name}.cfg" "${name%%_*}.tla" 2>&1)" || code=$?
    if ((code == 0)) && [[ "${out}" == *'No error has been found'* ]]; then
      printf '%-32s pass\n' "${name}.cfg"
    else
      printf '%-32s FAILED\n' "${name}.cfg"
      grep -v '^@!@!@' <<<"${out}" | tail -n 60 >&2
      status=1
    fi
  done
  rm -rf "${meta}"
  return "${status}"
}

main "$@"
