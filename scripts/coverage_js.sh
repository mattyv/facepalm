#!/bin/bash
#
# Runs each JavaScript source file in mcp/ and integrations/claude-code/
# against its own test file alone, under Node's test runner, with coverage
# scoped to that one file by --test-coverage-include, and holds it to 100
# percent of lines, branches and functions by itself (#67, #61 item 5): a
# file whose test mocks its collaborators, run alone, must cover only
# itself. Reads the LCOV report Node's coverage reporter writes for the
# numbers, rather than its own threshold flags or its included-files table:
# both pass even when the file never loaded at all, and the table's own
# merge across several child processes (main.js's tests, and the hook
# broker's chmod-denied fixture, each spawn more than one) is unreliable,
# unlike the one LCOV record per file.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ROOT
readonly OUT="${ROOT}/build-js-coverage"

# Merges every one of a source file's records in an LCOV report (there is
# more than one when a test re-imports it under a cache-busting query
# string, to force a second evaluation of its top-level code; each record
# then covers only what that one evaluation reached) and prints "FNF FNH
# LF LH BRF BRH": a function, line or branch counts as covered if any
# record shows it hit, and FNF/LF/BRF count the distinct ones ever seen.
# Prints nothing if the file has no record there at all. Read from a
# here-document, not passed inline, so kcov's line coverage of this script
# does not run across the awk program's own source lines.
read -r -d '' LCOV_SUMMARY_AWK <<'AWK' || true
$0 == "SF:" source { inside = 1; next }
$0 == "end_of_record" { inside = 0; next }
!inside { next }
/^FNDA:/ { split(substr($0, 6), p, ","); fnda[p[2]] += p[1]; seen = 1; next }
/^DA:/ { split(substr($0, 4), p, ","); da[p[1]] += p[2]; seen = 1; next }
/^BRDA:/ {
  split(substr($0, 6), p, ",")
  brda[p[1] SUBSEP p[2] SUBSEP p[3]] += (p[4] == "-" ? 0 : p[4])
  seen = 1; next
}
END {
  if (!seen) exit
  for (k in fnda) { fnf++; if (fnda[k] > 0) fnh++ }
  for (k in da) { lf++; if (da[k] > 0) lh++ }
  for (k in brda) { brf++; if (brda[k] > 0) brh++ }
  print fnf + 0, fnh + 0, lf + 0, lh + 0, brf + 0, brh + 0
}
AWK
readonly LCOV_SUMMARY_AWK

#######################################
# Prints the test file a source file's own tests live in, by this repo's
# naming convention: test/<name>.test.<ext>, in the test directory of its
# package (mcp, mcp/widget, or integrations/claude-code).
# Arguments:
#   The source file's path, relative to the repository root.
#######################################
test_for() {
  local source="$1" ext name dir
  ext="${source##*.}"
  name="$(basename "${source}" ".${ext}")"
  case "${source}" in
    mcp/widget/*) dir='mcp/widget' ;;
    mcp/*) dir='mcp' ;;
    *) dir='integrations/claude-code' ;;
  esac
  echo "${dir}/test/${name}.test.${ext}"
}

#######################################
# Prints LCOV_SUMMARY_AWK's result for one source file.
# Arguments:
#   The LCOV report's path.
#   The source file's path, as its SF: line names it.
#######################################
lcov_summary() {
  awk -v source="$2" "${LCOV_SUMMARY_AWK}" "$1"
}

#######################################
# Appends one shortfall line for a source file to OUT/shortfalls.txt: a
# missing test file, its test run failing outright, never loading the
# source at all, or any of its functions, lines or branches short of all
# of them.
# Globals:
#   OUT, ROOT
# Arguments:
#   The source file's path, relative to the repository root.
#######################################
check_source() {
  local source="$1" test_file lcov status=0
  test_file="$(test_for "${source}")"
  if [[ ! -f "${ROOT}/${test_file}" ]]; then
    echo "${source}: has no test file of its own (${test_file})" \
      >>"${OUT}/shortfalls.txt"
    return
  fi
  lcov="${OUT}/${source//\//_}.lcov"
  (cd "${ROOT}" && node --experimental-test-module-mocks \
    --experimental-test-coverage --test-coverage-include="${source}" \
    --test-reporter=lcov --test-reporter-destination="${lcov}" \
    --test-reporter=spec --test-reporter-destination=stdout \
    --test "${test_file}") || status=$?
  if ((status != 0)); then
    echo "${source}: its own test did not pass" >>"${OUT}/shortfalls.txt"
    return
  fi
  local summary fnf fnh lf lh brf brh
  summary="$(lcov_summary "${lcov}" "${source}")"
  if [[ -z "${summary}" ]]; then
    echo "${source}: its test run never loaded it" >>"${OUT}/shortfalls.txt"
    return
  fi
  read -r fnf fnh lf lh brf brh <<<"${summary}"
  if [[ "${fnf}" != "${fnh}" ]]; then
    echo "${source}: FN ${fnh} of ${fnf}" >>"${OUT}/shortfalls.txt"
  fi
  if [[ "${lf}" != "${lh}" ]]; then
    echo "${source}: L ${lh} of ${lf}" >>"${OUT}/shortfalls.txt"
  fi
  if [[ "${brf}" != "${brh}" ]]; then
    echo "${source}: BR ${brh} of ${brf}" >>"${OUT}/shortfalls.txt"
  fi
}

main() {
  rm -rf "${OUT}" && mkdir -p "${OUT}"
  touch "${OUT}/shortfalls.txt"
  local listing source
  local -a patterns=()
  patterns+=('mcp/*.js')
  patterns+=('mcp/widget/*.js')
  patterns+=('integrations/claude-code/*.mjs')
  patterns+=('integrations/claude-code/*/*.mjs')
  listing="$(git -C "${ROOT}" ls-files "${patterns[@]}" | grep -v '/test/')"
  while IFS= read -r source; do
    check_source "${source}"
  done <<<"${listing}"
  if [[ -s "${OUT}/shortfalls.txt" ]]; then
    cat "${OUT}/shortfalls.txt" >&2
    return 1
  fi
}

main "$@"
