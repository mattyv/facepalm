# Working rules for agents in this repository

Rules for a coding agent working on facepalm. Read them at the start of every
session, and add a rule here whenever the user sets one. They were carried over
from [ai-pair](https://github.com/mattyv/ai-pair)'s working rules on
2026-11-15, when the user chose ai-pair's full quality bar for this repository,
and adapted to C++.

## What facepalm is

A local perception service: a C++ helper that reads the camera, runs the face
model, and reports reactions such as nods, shakes and engagement to client
tools. The design and its decisions are on
[ai-pair#83](https://github.com/mattyv/ai-pair/issues/83) and in ai-pair's
`blueprints/ideas/nonverbal-perception-service.md`.

## Issues and governance

-   **Issues live on `mattyv/facepalm`,** and they are the knowledge surface:
    findings, evidence and working notes go in issues, not in files here.
-   **Decisions are logged in [docs/decisions.md](docs/decisions.md)** (set by
    the user on 2026-11-20): when a decision is made or reversed, add an entry
    at the top in the same PR, with the date, the decision, why, what was
    rejected, and the issue that holds the evidence. Never edit an old entry;
    mark it as replaced.
-   **The issue exists first, and the branch is named for it:**
    `<type>/<issue>-<slug>`, with `<type>` one of `feat`, `fix`, `docs`,
    `chore`. Work happens in a worktree under `.claude/worktrees/<slug>/`; the
    main checkout stays on `main`, moved only by fast-forward.
-   **Naming an issue authorizes its whole cycle:** branch, spec, tests, code,
    docs, PR, CI green, squash-merge, branch deleted, issue closed. The push,
    the merge and the branch deletion still wait for the user's explicit
    word, asked for once, in one line, when the PR is ready.
-   **New issues** open with one plain sentence saying what they are, carry a
    severity label (`severity: critical`, `high`, `medium`, `low`) when they
    are findings or `enhancement` when they are requests, and reference code
    as permalinks pinned to a commit.
-   **A finding is fixed where it is found, or filed.** Small: fixed in the
    same branch and listed in the PR. Significant: an issue filed before the
    closing report, never offered back as a choice.
-   **Questions are questions.** A question from the user is answered, not
    executed. Work starts on a verb-first instruction.
-   **The agent does not lower any bar here.** A step that looks heavy is done,
    or the user is asked; it is never skipped on the agent's own judgment.

## Building new code

-   **Lean on existing working code as a reference** (set by the user on
    2026-11-15). For each part, name the proven implementation it follows,
    such as MediaPipe's own C++ or ai-pair's audio helpers, read it, and say
    which parts are followed and which differ. Where possible, use the
    reference as the test oracle. Check the licence before porting any code.
-   **No throwaway languages** (set by the user on 2026-11-15). A spike is the
    first cut of the real code, in C++. Golden fixtures come from MediaPipe's
    own C++ build, not its Python package.
-   **Test first.** Write the test, watch it fail for the right reason, then
    write the code.
-   **Spec first** for anything with a lifecycle or concurrency, such as the
    camera, the helper process and client subscriptions: write its TLA+ spec,
    check it with TLC, prove each invariant bites with a mutation, then write
    the code that follows it.

## Tests

-   **A unit test is solitary:** it tests one source file, with every
    collaborator replaced by a mock (GoogleMock). Pure computation, constants
    and the standard library stay real.
-   **Each source file has its own test file and executable:** `src/x.cc` is
    tested by `test/x_test.cc`, built as `x_test`. `facepalm_unit()` in
    `CMakeLists.txt` sets this up.
-   **A test file covers its source file 100 percent by itself:** lines,
    branches and functions. `scripts/coverage.sh` measures each source from
    its own executable only, against `coverage_thresholds.json`.
-   **JavaScript is held to the same 100 percent** (set by the user on
    2026-11-21, #67): each file in `mcp/` and `integrations/claude-code/`
    is covered for lines, branches and functions by its own test file,
    `x.js` by `test/x.test.js`, with its collaborators mocked.
-   **Shell is held to 100 percent of lines too** (set by the user on
    2026-11-21, #2): `scripts/coverage_shell.sh` runs every bats test under
    kcov on CI's Linux job and fails a script below 100. Write scripts so
    kcov counts no line bash never traces: an awk program in a
    here-document, a long list built with `+=` a line at a time, each
    `$(...)` on one line, and a `while read` loop over a here-string rather
    than `done <file`.
-   **A coverage floor is never lowered.** A run below one gets the test cases
    that bring it back up.
-   **Every test is idempotent and cleans up after itself.**
-   **Every file type with a test tool has tests:** a shell script gets a bats
    test in a `test/` directory beside it, against fakes on the PATH.
-   **The fixture tool in `tools/fixtures` is the exception to the unit test
    rules:** Bazel builds it inside MediaPipe's workspace, not CMake, so it
    has no GoogleTest executable or coverage floor. Its test is
    `check_portrait.sh`, which holds its output to MediaPipe's own expected
    results; clang-format checks its style, and clang-tidy does not run on
    it.
-   **The MediaPipe shim in `src/mediapipe_shim` is held to 100 percent all
    the same,** though Bazel builds it: `tools/fixtures/test_shim.sh` runs its
    GoogleTest tests against the real face landmarker, with its source
    instrumented, and fails below 100 percent of lines, branches, functions
    or regions. A result shape a real model never returns is tested through
    `facepalm_mediapipe_internal.h` with a made-up result. clang-tidy does
    not run on it.
-   **In a bats test, end a `[[ ]]` or `(( ))` assertion with `|| false`:**
    under bash 3.2, which macOS ships, a false one before a test's last line
    does not fail the test (#22). `scripts/check_bats.sh` fails on one that
    does not, in CI's Shell step.

## Style

-   **C++ follows Google's C++ Style Guide**
    (https://google.github.io/styleguide/cppguide.html), checked by
    clang-format (`.clang-format`) and clang-tidy (`.clang-tidy`), and both
    fail CI on any finding. The checks turned off in `.clang-tidy` are listed
    there with the reason for each. A `NOLINT` names its check and says why.
-   **Shell follows Google's Shell Style Guide,** as in ai-pair: ShellCheck
    with the optional checks in `.shellcheckrc`, shfmt with `.editorconfig`,
    `#!/bin/bash`, 80 columns, at most 100 lines, bash 3.2.
-   **Markdown follows Google's Markdown style guide,** checked by
    markdownlint with `.markdownlint-cli2.jsonc`.
-   **Every change ships with its docs in the same PR.**
