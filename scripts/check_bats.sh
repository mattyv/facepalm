#!/bin/bash
#
# Fails on a bats assertion, a [[ ]] or (( )) on a line of its own, that does
# not end in || false. Under bash 3.2, which macOS ships, a false one before a
# test's last line does not fail the test (#22). The check is ai-pair's, from
# its scripts/lint_shell.sh (#112).
#
# Usage: check_bats.sh [file.bats...]; by default every .bats file in git.

set -euo pipefail

main() {
  local -a files=("$@")
  if ((${#files[@]} == 0)); then
    local listing file
    listing="$(git ls-files --cached --others --exclude-standard '*.bats')"
    while IFS= read -r file; do
      [[ -n "${file}" ]] && files+=("${file}")
    done <<<"${listing}"
  fi
  if ((${#files[@]} == 0)); then
    return 0
  fi
  awk '
    /^[ \t]+(\[\[|\(\().*(\]\]|\)\))[ \t]*$/ {
      printf "%s:%d: end the assertion with || false\n", FILENAME, FNR \
        > "/dev/stderr"
      bad = 1
    }
    END { exit bad }
  ' "${files[@]}"
}

main "$@"
