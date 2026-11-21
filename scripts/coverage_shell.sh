#!/bin/bash
#
# Runs every bats test in git under kcov, as ai-pair's coverage_shell.sh
# does, and holds each shell script in git to its shell lines floor in
# coverage_thresholds.json, 100 when none is listed (#2). Linux only: on
# macOS, kcov's bash tracing rides on stderr, which bats captures. Tests run
# copies of the scripts, so a script's lines count over every copy kcov saw.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ROOT
readonly OUT="${ROOT}/build-shell-coverage"
readonly THRESHOLDS="${ROOT}/coverage_thresholds.json"

#######################################
# Prints "script line covered" for each line of each listed script that kcov
# saw: covered is 1 when any copy of the script ran the line, else 0.
# Arguments:
#   A file listing the scripts, one a line; kcov's Cobertura reports.
#######################################
tally() {
  awk '
    FNR == NR { scripts[$0] = 1; next }
    FNR == 1 { source = "" }
    match($0, /<source>[^<]*<\/source>/) {
      source = substr($0, RSTART + 8, RLENGTH - 17)
    }
    match($0, /filename="[^"]*"/) {
      file = substr($0, RSTART + 10, RLENGTH - 11)
      if (file !~ /^\//) file = source "/" file
      script = ""
      for (candidate in scripts) {
        if (substr(file, length(file) - length(candidate)) == "/" candidate \
          && length(candidate) > length(script)) script = candidate
      }
    }
    script != "" && match($0, /<line number="[0-9]+" hits="[0-9]+"/) {
      split(substr($0, RSTART, RLENGTH), parts, "\"")
      key = script SUBSEP parts[2]
      lines[key] = 1
      if (parts[4] > 0) hit[key] = 1
    }
    END {
      for (key in lines) {
        split(key, parts, SUBSEP)
        print parts[1], parts[2], (key in hit)
      }
    }
  ' "$@"
}

main() {
  cd "${ROOT}"
  rm -rf "${OUT}"
  mkdir -p "${OUT}"
  git ls-files '*.sh' >"${OUT}/scripts.txt"
  git ls-files '*.bats' >"${OUT}/tests.txt"
  local tests run script counts covered total missed percent floor
  local shortfalls=''
  local -a reports=()
  while IFS= read -r tests; do
    run="${OUT}/runs/${tests//\//_}"
    # Debian's kcov writes its bash helper there before it makes the folder.
    mkdir -p "${run}"
    kcov --include-pattern=.sh --exclude-pattern=/test/ "${run}" \
      bats "${tests}"
  done <"${OUT}/tests.txt"
  find "${OUT}/runs" -name cobertura.xml >"${OUT}/reports.txt"
  while IFS= read -r run; do
    reports+=("${run}")
  done <"${OUT}/reports.txt"
  tally "${OUT}/scripts.txt" "${reports[@]}" | sort -k1,1 -k2,2n \
    >"${OUT}/tally.txt"
  while IFS= read -r script; do
    counts="$(awk -v s="${script}" '$1 == s { t++; c += $3 }
      $1 == s && !$3 { m = m (m == "" ? "" : ",") $2 }
      END { if (t) print c, t, m }' "${OUT}/tally.txt")"
    if [[ -z "${counts}" ]]; then
      shortfalls+="${script}: no test ran it"$'\n'
      continue
    fi
    read -r covered total missed <<<"${counts}"
    percent="$(awk -v c="${covered}" -v t="${total}" \
      'BEGIN { printf "%.2f", 100 * c / t }')"
    echo "${script} lines ${percent}% (${covered} of ${total})"
    floor="$(jq -r --arg f "${script}" '.[$f].shell.lines // 100' \
      "${THRESHOLDS}")"
    if awk -v a="${percent}" -v f="${floor}" 'BEGIN { exit !(a < f) }'; then
      shortfalls+="${script}: lines ${percent}% is below ${floor}%,"
      shortfalls+=" missing ${missed}"$'\n'
    fi
  done <"${OUT}/scripts.txt"
  if [[ -n "${shortfalls}" ]]; then
    printf '%s' "${shortfalls}" >&2
    return 1
  fi
}

main "$@"
