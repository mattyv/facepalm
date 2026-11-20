# Decisions

Each decision made about facepalm and why, newest first. An entry is short: the
evidence and the discussion are in the issue it names. An entry is never
edited once made; a decision that is reversed gets a new entry, and the old one
a line saying which entry replaced it.

## 2026-11-20: Use an ELF export list for the Linux MediaPipe shim

-   **Decision:** build a `.so` on Linux with the same six-function C ABI as
    the macOS dylib, hiding all other symbols with an ELF version script.
-   **Why:** MediaPipe's internal symbols must not collide with another copy.
-   **Rejected:** exporting all symbols or passing Mach-O flags to Linux.
-   **Cost:** the ELF boundary was cross-linked with synthetic symbols; the
    full MediaPipe build and runtime have not yet been verified on Linux. This
    does not supply a Linux camera backend or a runnable Linux daemon.
-   **Evidence:** [#3](https://github.com/mattyv/facepalm/issues/3).

## 2026-11-20: Store feedback only when explicitly requested

-   **Decision:** `--ratings-file` enables a private, single-writer versioned
    JSONL feedback file capped at 10 MiB. A rating succeeds only after its
    record is appended and synced. It records the delivered interpretation,
    reading ID, publication and rating times, and score.
-   **Why:** explicit local feedback can support future investigation without
    storing frames, model inputs, credentials or client identity.
-   **Rejected:** silently discarding ratings or automatically tuning from
    them. No rating is accepted without a configured feedback sink.
-   **Cost:** a partial write or sync failure disables the sink for that run;
    a failed sync may nevertheless leave an unacknowledged record. Incomplete
    files need manual recovery. Existing files are capped, never truncated.
-   **Replaces:** the earlier daemon decision to reject all ratings.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: Use a native menu and next-start settings on macOS

-   **Decision:** use an AppKit menu-bar host and native settings window. It
    edits the daemon's existing config format, including gesture and expression
    tuning, preserving comments and other settings. It saves atomically after
    checking the file snapshot.
-   **Why:** the current daemon has no status or settings control API; the
    native host provides the requested controls without inventing live state.
-   **Rejected:** implying the camera is running, or starting the daemon from
    the menu before there is a reviewed consent and lifecycle interface.
-   **Cost:** settings apply only when a daemon is started with `--config` and
    the shown path. Camera status is unavailable. Snapshot checking detects
    prior external edits but is not a cross-process compare-and-swap; a writer
    racing the final save can still be overwritten. The host does not stop a
    separately running daemon when it quits.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: Gate the foreground daemon by consent and a private token

The rating rejection in this entry is replaced by the feedback decision above.

-   **Decision:** the daemon starts only with explicit launch consent, a
    readable model, and a user-private token file. One connection presenting
    that token admits the camera; disconnect revokes its subscription. Each
    connection gets a distinct internal identity and no credentials enter the
    feed. Stop waits for camera closure and model retirement.
-   **Why:** a same-user socket connection or a self-asserted name alone is not
    proof that the user approved a client.
-   **Rejected:** opening the camera at login or treating installation as
    consent. The token must be provisioned by an explicit approval step.
-   **Cost:** one approved connection at a time; no approval UI or feedback
    storage exists in this slice, so daemon ratings are rejected. Consent is
    launch-scoped and stopping the process revokes it. Only a supplied
    `--config` file is loaded. This has not been exercised on a live camera.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: Expose the reaction feed through a stdio MCP adapter

-   **Decision:** each MCP process owns one uniquely named socket session and
    forwards bounded waits and ratings to the existing feed. The adapter uses
    the official Node MCP SDK as a maintained production adapter; it does not
    own or start the camera.
-   **Why:** an agent can use the same interpreted feed and rating rules as
    other clients without a second cursor or a new wire protocol.
-   **Rejected:** silently reconnecting after cancellation: a native wait may
    already have consumed a reading, and a new session has a different cursor.
-   **Cost:** cancelling an active request closes the session and can lose an
    unseen delivery; the native wait may run until its timeout. The feed records
    delivery to MCP, not confirmation that the host displayed the summary.
    Socket hello names are self-asserted, not client authentication.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: A turn is one excursion from a starting pose

-   **Decision:** after a turn, suppress further turns until yaw returns within
    ten degrees of the estimated starting yaw and the model reads no gesture
    for 750 ms. The estimate is the oldest yaw in the model window when the
    turn candidate starts. Other gesture classes retain their existing rule.
-   **Why:** a turn and its return were reported repeatedly as separate turns.
-   **Rejected:** treating any held pose as a completed excursion: it reports
    the return as another turn.
-   **Cost:** ten degrees is based on one person's capture; a different return
    pose can suppress a later turn. Tracking loss starts a new track, without
    evidence of return. A head turn does not establish gaze or attention.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: Run native tests under Clang sanitizers

-   **Decision:** run the native CTest suite on Linux with AddressSanitizer and
    UndefinedBehaviorSanitizer together, failing on the first finding.
-   **Why:** coverage and clang-tidy do not detect all exercised memory errors
    and undefined behavior at runtime.
-   **Rejected:** replacing the existing coverage or clang-tidy checks; each
    finds different problems.
-   **Cost:** this checks exercised Linux CMake code only. It does not cover the
    macOS camera, the separately built MediaPipe shim, or unexecuted paths.
-   **Evidence:** the user's request for sanitizer checks in CI.

## 2026-11-20: Replay recorded head gestures without a camera

-   **Decision:** replay the probe's raw gesture frames through the live window,
    model and reporting gate; new recordings mark each loss of a face.
-   **Why:** recorded frames make gesture classification and tracking gaps
    inspectable without another camera session.
-   **Rejected:** treating a timestamp gap as lost tracking: dropped frames and
    no-face callbacks cannot be distinguished by elapsed time alone.
-   **Cost:** older logs lack no-face markers, so their replay cannot reliably
    reproduce tracking resets. Replay uses the default confidence threshold.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: Expression reading pauses while the head moves

-   **Decision:** while the head is away from its resting pose, or has just
    moved, the expression reader neither reports nor learns.
-   **Why:** the face model's expression scores follow head pose. With no
    expression asked for, nods, shakes and tilts were reported as about 40
    expressions in 85 seconds.
-   **Rejected:** pausing on distance from the resting pose alone, which left
    24 false reports; pausing on head speed, which swallowed a real squint.
-   **Cost:** an expression made while the head moves is reported only if it
    is still held shortly after the head settles.
-   **Evidence:** [#46](https://github.com/mattyv/facepalm/issues/46).

## 2026-11-20: A head gesture is reported once per movement

-   **Decision:** a gesture is reported once it has held for a moment, and
    nothing more is reported until the head has been still.
-   **Why:** within one movement the model's answer alternates between related
    gestures, such as shake and turn, and each change was being reported. The
    model's authors do not report its raw answers either; this follows their
    smoothing, in a form that does not wait for the movement to end.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: A frown and a squint are read apart

-   **Decision:** lowered brows and narrowed eyes are separate expressions.
-   **Why:** a small frown moves the brow scores alone, and averaged with the
    squint scores its rise was lost. A squint therefore reports both.
-   **Replaces:** the plan in #46's description to report them as one.
-   **Evidence:** [#46](https://github.com/mattyv/facepalm/issues/46).

## 2026-11-20: Facial expressions come from the face model's own scores

-   **Decision:** expressions are read from the 52 blendshape scores
    MediaPipe's face landmarker already produces, each judged against the
    person's own resting face. facepalm trains no model of its own for this.
-   **Why:** measured on one person's small, natural expressions, a brow
    raise, a smile and pressed lips stand 20 to 90 times above the resting
    spread. The scores cost nothing extra and are free to ship.
-   **Known weakness:** the frown, at about 6 times the resting spread.
-   **Rejected:** OpenFace 2.2 and 3.0, whose licence allows noncommercial
    research use only and forbids passing the software on; ready-made emotion
    models, which label happy, sad and angry and not confused or
    concentrating, read a face picture and not the scores, and are trained on
    data with noncommercial terms.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35),
    [#46](https://github.com/mattyv/facepalm/issues/46).

## 2026-11-20: facepalm is licensed GPL-3.0-only

-   **Decision:** facepalm's licence changes from MIT to GPL-3.0-only.
-   **Why:** Idiap's head-gesture code and model are GPL-3.0-only, and facepalm
    contains a port of both.
-   **Rule that follows:** a model under the GPL is acceptable; one under
    noncommercial terms is not.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: Head gestures use Idiap's CCDbHG model, alone

-   **Decision:** nods, shakes, tilts, turns and up-down movements are read by
    a C++ port of Idiap's CCDbHG model. Nodding Pigeon is removed.
-   **Why:** Idiap's model was trained on natural conversation and reads the
    landmarks and head pose facepalm already has. Live, it caught one person's
    small nods, a single quick nod and small shakes, with nothing reported
    while still.
-   **Rejected:** Nodding Pigeon, which rounds face positions to 1 percent of
    the picture and so loses small movements, and which added nothing beside
    the other model; a detector of our own on head angles, kept only as a
    fallback had no existing model worked.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: TLC is pinned to its stable release

-   **Decision:** CI downloads TLC 1.7.4.
-   **Why:** the 1.8.0 pre-release's file is rebuilt in place, which broke the
    pinned checksum and failed every pull request.
-   **Evidence:** [#48](https://github.com/mattyv/facepalm/issues/48).

## 2026-11-20: Decisions are logged here

-   **Decision:** this file holds each decision and why; the issue it names
    holds the evidence and the discussion.
-   **Why:** the reasons behind one day's model choices were spread over four
    issues and their comments.
-   **Replaces:** the rule that decisions go in issues only.
-   **Evidence:** [#54](https://github.com/mattyv/facepalm/issues/54).

## 2026-11-18: How reactions reach an agent

-   **Decision:** facepalm has its own MCP server. Where a client supports
    hooks, reactions are delivered automatically after each agent action; the
    MCP tools are the fallback.
-   **What the agent gets:** interpreted reactions (a frown, a smile, a nod,
    and what each is taken to mean), not raw data, and only what happened since
    it last looked.
-   **What the person sees:** a one-line summary written by facepalm, carrying
    a reading ID, which the agent shows as it is.
-   **Feedback:** a rating from 1 to 5, given through the MCP or in the AI Pair
    panel, and only within five minutes of the reading being shown.
-   **Also decided:** a settings page in the browser, served locally;
    calibration is optional; Ubuntu is supported; ChatGPT's desktop app is out
    of scope.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-18: Generic readings first, light personal tuning after

-   **Decision:** reactions are read with rules that are the same for
    everyone, and tuned for each person from their ratings.
-   **Why:** most expressions are small, the generic readings are expected to
    be right about nine times in ten, and calibration up front is a burden.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35).
