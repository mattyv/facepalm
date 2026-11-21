# Decisions

Each decision made about facepalm and why, newest first. An entry is short: the
evidence and the discussion are in the issue it names. An entry is never
edited once made; a decision that is reversed gets a new entry, and the old one
a line saying which entry replaced it.

## 2026-11-21: Build and test the Linux camera's sources in CI

-   **Decision:** CI's `linux` and both sanitizer jobs fetch MediaPipe's C
    headers and `libmediapipe.so` with `scripts/fetch_mediapipe_c.sh`, each
    file checked against `scripts/mediapipe_c.sha256` and the folder cached,
    and build the Linux daemon with them. `src/linux/mediapipe_c_shim.cc`
    and `src/linux/v4l2_session.cc` are units of their own, held to 100
    percent by `test/linux/*_test.cc`, whose fakes stand in for MediaPipe's
    C API, libv4l2 and `poll` at link time. Their floors count on Linux
    only.
-   **Why:** no job built either file or the daemon, so a mistake in them,
    such as in the shim's result-shape checks, would show only on a real
    Ubuntu run.
-   **Rejected:** fetching the whole MediaPipe checkout for nine headers;
    pinning only the wheel's hash, which leaves the headers to a git commit;
    and an interface injected into `V4l2Session` for its system calls, a
    larger seam than the link-time one `test/face_model_test.cc` already
    uses. Evidence: [#83](https://github.com/mattyv/facepalm/issues/83).

## 2026-11-21: The feeling the person picks tunes the next read, in the daemon

-   **Decision:** a client sends `feel <interpretation-id> <feeling>` when
    the person says how they really felt at a beat. The service takes it only
    for an interpretation it gave that connection, once, within five minutes,
    and only a feeling from `src/feelings.h`; it answers `{"ok":true}` or an
    error. `reset feelings` forgets every pick. Each beat's interpretation
    also lists every feeling, so clients take the names from the service. The
    daemon counts picks of each feeling for each reaction and kind of beat, in
    one file private to the person (`facepalm/feelings` in `$XDG_CONFIG_HOME`
    or `~/.config`, or `--feelings-file`), which it alone writes: it holds a
    lock file while open, saves one pick at a time by renaming a synced
    temporary file over it, and answers ok only once the folder is synced. A
    feeling picked at least twice and more than half the time is the
    person's read for that reaction and beat, once a held-out check passes:
    each pick in turn is held out and read from the others, and those reads
    must be right more often than the generic rule. Three matching picks are
    enough. The MCP adapter's `tell_feeling` replaces `rate_reaction`, and
    `/facepalm:feel` replaces `/facepalm:rate`. `specs/FeelingFeedback.tla`
    models it.
-   **Why:** the user chose "pick the feeling" over the 1 to 5 rating, and
    feelings and their learning in the service, so every client feeds the
    same corrections (#61). Generic reads first, then light personal tuning
    (#35): the check keeps a person's picks from changing a read until they
    would have predicted that person better than the rule.
-   **Rejected:** the 1 to 5 rating, `rate_reaction`, the ratings file and
    `specs/RatingPersistence.tla`, which a rating needed. Codex's draft
    `meaning-model.js`, whose held-out idea this keeps: it learned only
    agreement and disagreement from nods and shakes, held out the last closed
    session, which needs a session lifecycle no client has, and compared
    against the most common label rather than the generic rule; the check
    here holds out each pick, so plain counts suffice. A machine-learning
    framework, which a few counts do not need. Keeping summaries, reading IDs
    or times in the file.
-   **Cost:** picks never fade; a reset clears them all. A beat with reactions
    of more than one kind, or with a dropped reading, takes a feeling but
    teaches nothing, since it cannot say which reaction it was about. A frown
    already reads as confused, so picking confused for it changes nothing. A
    save whose folder sync fails is answered with an error, yet its pick is in
    the file and is read. One daemon at a time can open the file.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35),
    [#61](https://github.com/mattyv/facepalm/issues/61).

## 2026-11-21: Remove the Linux ELF build of the MediaPipe shim

-   **Decision:** remove the Bazel `libfacepalm_mediapipe.so` target, its
    ELF export list and the bats test that cross-linked it;
    `tools/fixtures/build.sh` refuses to run on Linux, naming
    `docs/linux-camera.md`. This replaces the 2026-11-20 entry "Use an ELF
    export list for the Linux MediaPipe shim".
-   **Why:** nothing used it. Since #60, the Linux daemon compiles
    `src/linux/mediapipe_c_shim.cc` in and links MediaPipe's own
    `libmediapipe.so` from its 0.10.35 wheel; CMake imports the Bazel shim
    on macOS only, and no workflow built the `.so`, so its 100 percent rule
    never ran.
-   **Rejected:** building and testing the `.so` on CI's Linux, which would
    add a half-hour Bazel build for a library no Linux build links.
-   **Evidence:** [#65](https://github.com/mattyv/facepalm/issues/65).

## 2026-11-21: Run the native tests under ThreadSanitizer too

-   **Decision:** a CI job of its own, on Linux, builds the native CTest suite
    with `-fsanitize=thread` and fails on the first data race it reports.
    It sets `vm.mmap_rnd_bits=28` first, since ThreadSanitizer does not start
    under the 32 bits of address space randomization newer kernels use.
-   **Why:** the service, the feed and the client gate run on threads, and
    AddressSanitizer and UndefinedBehaviorSanitizer find no data races. With
    the lock around `connection.done = true` in `Service::Serve` removed,
    `service_test` failed under ThreadSanitizer with a data race against
    `AcceptAll`, three runs out of three; with it, the suite is clean.
-   **Rejected:** adding the flag to the existing sanitizers job, which
    Clang refuses: ThreadSanitizer cannot be combined with AddressSanitizer.
-   **Cost:** as with the other sanitizers, only threads the tests start are
    checked, and only on Linux; the MediaPipe shim is not in the build.
    Evidence: [#66](https://github.com/mattyv/facepalm/issues/66).

## 2026-11-21: Skip a degenerate pose's frame in the expression reader

-   **Decision:** the expression reader drops a face whose transformation
    matrix has no rotation to read, as it would a frame with no face: no
    scores are judged and nothing is learned from it. The looking-away
    reader already did the same.
-   **Why:** averaged into `ExpressionTracker`'s resting pose, one such
    frame left it NaN until the next gap of more than two seconds, and the
    pose gate never shut: with a degenerate frame before it, a smile held
    while the head turned 25 degrees away was reported, where without one
    it was not.
-   **Rejected:** judging the frame's scores with the pose left out, which
    would let scores the pose has moved through ungated; and having
    `ExpressionTracker` ignore a non-finite pose itself, where
    `ReactionFrom` and the looking-away reader check the matrix as they
    read the pose from it, and this reader now does the same.
-   **Narrows:** the 2026-11-21 entry for #44, which left the other readers
    seeing such a face. Evidence:
    [#79](https://github.com/mattyv/facepalm/issues/79).

## 2026-11-21: Read a reaction as a feeling when a client marks a beat

A client that has just asked a question or made a proposal sends
`beat question|proposal <milliseconds>`. The service waits out that window
(1 to 10000 ms) and replies with the readings since the client last looked
and one `interpretation`: an ID, the beat's kind, and one of six feelings.
`src/feelings.h` is the only list of them: agreement, disagreement,
engagement, annoyance, confused and unknown; the MCP adapter checks only that
a feeling is a name. The generic rule reads only the readings made during the
window: nods as agreement, shakes as disagreement, brows down or narrowed
eyes (which the expression reader reports only once held) as confused. No
cue, cues of different feelings, or a window with a reading dropped before it
ended read as unknown; so does a reaction that comes after the window, which
goes to the next read instead. No generic rule gives engagement or annoyance
yet: they exist so a correction (#35, item 4 of #61) can name them. The
window opens when the service gets the line, so a reaction made while the
person was still reading the question is missed. `specs/FeelingBeat.tla`
models it. `rate_reaction` and the ratings file stay until item 5 replaces
them.

Rejected: reading the beat in the MCP adapter, as Codex's draft did
(`groundedWait`), because feelings and their tuning belong to the service, so
every client feeds the same corrections (#61); and its
`tentative_agreement` names, for the six feelings the user chose. Evidence:
[#70](https://github.com/mattyv/facepalm/issues/70),
[#61](https://github.com/mattyv/facepalm/issues/61).

## 2026-11-21: Read looking away from the resting pose, held a second

-   **Decision:** report `{"gesture":"looking_away"}` once when the head's
    yaw or pitch stays more than 15 degrees from its resting pose for 1,000
    ms, and again only after it comes back within half that. The resting
    pose is `ExpressionTracker`'s, learned from the same faces with the
    expression settings; nothing is reported while it is being learned.
    `looking_away.enabled`, `.degrees` and `.hold_ms` are in the config file.
    `specs/LookAway.tla` models the rule.
-   **Why:** #35's design reads reactions from head pose and blendshapes
    judged against the person's own resting state, with no calibration up
    front. On one person's recordings, replayed through the detector, each
    prompted look down and each turn the face model kept tracking was
    reported once, and nods, which stay within about 12 degrees and leave
    for under half a second, were not.
-   **Rejected:** reading gaze, which needs eye landmarks no reader uses yet
    (#61 item 11); counting roll, which is a tilt, not a look away; and a new
    reaction key, such as `attention`, which the daemon's feed filter and
    every client would have to learn.
-   **Cost:** a pose held for long enough, under a minute, becomes the resting
    pose, so turning back from it is reported as a look away. A face the
    model loses, as on a wide turn, gives no frames and so no report. The
    thresholds come from one person's recordings.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35).

## 2026-11-21: Hold the shell scripts to 100 percent of lines with kcov on Linux

`scripts/coverage_shell.sh` runs every bats test under kcov, in a Debian
trixie container on CI, and holds each shell script to 100 percent of its
lines (its `shell` floor in `coverage_thresholds.json`), as the C++ and
JavaScript are held. The tests run copies of the scripts in temporary
folders, so a script's lines are counted over every copy. kcov counts lines
that bash never traces: each line of a multi-line quoted string or array, a
`$(...)` continued over lines, and `done <file`. The scripts are written so
none occurs: awk programs come from here-documents, which kcov skips; long
lists are built with `+=`; each `$(...)` sits on one line; and loops read
here-strings. Rejected: floors at the measured number for the lines kcov
miscounts (this PR's first version), since the user holds every file to 100;
kcov on macOS, where its bash tracing rides on stderr and bats captures it,
so every script reads 0 (ai-pair's finding; here, Homebrew's kcov 43 running
one bats file had not finished after two minutes); bashcov, a Ruby gem
neither repository uses; and Ubuntu's runner, which does not package kcov.
Evidence: [#2](https://github.com/mattyv/facepalm/issues/2).

## 2026-11-21: A turn's suppression ends once the head settles anywhere

-   **Decision:** after a turn, later turns stay suppressed until the model
    reads no gesture for 750 ms with yaw back within ten degrees of the
    estimated starting yaw, as before, or for 2,000 ms with yaw within ten
    degrees of wherever the head settled. The pose it settled in becomes the
    origin, and the next turn is reported. `TurnExcursion.tla` gains the
    settled hold and a liveness property, `TurnRearms`.
-   **Why:** the suppression had no bound: someone who turned to a second
    screen, or settled 15 degrees off, got no turns for as long as they
    stayed (#62), and looking away (#35) needs turns to keep reporting.
-   **Rejected:** releasing after the general gate's 750 ms of stillness
    anywhere, which reports a glance's return as a second turn, the problem
    the turn latch was made for; and a plain timeout from the turn, which
    releases mid-movement and can report the same excursion twice.
-   **Cost:** a head held away for two seconds and then turned back gives two
    turns, one each way. Two seconds is the limit NHTSA's 2013 visual-manual
    distraction guidelines set for a single glance, taken from memory and not
    measured on facepalm's users; a longer glance reports its return.
-   **Replaces:** the suppression rule in "A turn is one excursion from a
    starting pose" (2026-11-20).
-   **Evidence:** [#62](https://github.com/mattyv/facepalm/issues/62).

## 2026-11-21: Bound camera_probe's recording and fail an incomplete one

The probe's recorder holds at most 30 frame copies waiting to be written
(two seconds at 15 frames a second, about 35 MiB at 640x480), and drops a
frame past that. The probe exits only after every frame it accepted is
written or has failed, and exits with 1 when any frame was dropped or not
written; a frame's capture time goes in `timestamps.txt` only once its PNG
is written. Rejected: blocking the capture queue on a full backlog, which
would stall the camera and the face model with it, and exiting without
waiting, which reported success with frames still unwritten. A disk that
never answers now leaves the probe waiting rather than exiting. Evidence:
[#41](https://github.com/mattyv/facepalm/issues/41).

## 2026-11-21: Send no head-angle reaction for a degenerate pose

When the face's transformation matrix has no rotation to read (all zeros, or
holding NaN or infinity), the head-pose reader sends nothing for that frame,
rather than JSON holding `nan`, which no client can parse. The other readers
still see the face. Rejected: sending `null` angles, which every client would
have to handle for a case of no use to it, and sending zeros, which reads as
a head facing the camera. Evidence:
[#44](https://github.com/mattyv/facepalm/issues/44).

## 2026-11-21: Remove only facepalm's own socket at the socket path

At startup, facepalm still replaces a socket that nothing answers on, so it
starts again after a crash without help. It now refuses a path that holds
anything other than a socket, and leaves it alone. On close, it removes the
socket file only if it is still the one it bound, so it cannot delete a newer
facepalm's socket. Rejected: never removing an existing socket (a draft on
`fix/delivery-audit`), because a daemon started from the menu after a crash
could not recover without someone deleting the file by hand. Evidence:
[#37](https://github.com/mattyv/facepalm/issues/37).

## 2026-11-20: Use the macOS settings layout on Linux

Use Qt Widgets to present the same detection controls and field order as the
macOS settings window. Both hosts use the shared config parser and document
editor, which preserve comments and unrelated values. The desktop launcher
opens Settings directly, and the tray keeps service readiness and replay
status. This replaces the status-only Linux window so users can edit the
daemon configuration on either desktop.

Settings apply at the next daemon start with `--config` and the displayed
path. Save checks for changes since opening the form and writes atomically.
A writer racing that final save can still overwrite or be overwritten.
[Facepalm service issue #37](https://github.com/mattyv/facepalm/issues/37)

## 2026-11-20: Run the Linux camera through the shared daemon

Use Video4Linux through libv4l2 and link the pinned MediaPipe Tasks C
library on Linux. The capture adapter joins its worker before reporting
retirement, and the existing consent, session, frame, and feed owners
keep their responsibilities. Serial queues run the same daemon composition
as macOS without requiring libdispatch on Linux.

The build keeps the existing FaceModel interface and uses the official C
library to avoid a separate application or an inference service. It needs
matching 0.10.35 headers and library and a Face Landmarker model. The first
Linux setup uses one selected single-plane camera. Restart after reconnecting
a disconnected device. The development daemon installs separately from the
tray `.deb`.

On Debian 13, the integrated camera bound at `/dev/video0`, MediaPipe
processed a live frame, and MCP returned a classified turn. A refused
client left the camera closed. Two consecutive approved MCP sessions each
opened, processed, and released the device. [Facepalm service issue #37](https://github.com/mattyv/facepalm/issues/37)

## 2026-11-20: Package the Linux tray and replay tools for Debian 13

Use Qt 6 Widgets for the Linux tray, menu and status window. Keep the
existing shared core and macOS AppKit host. Qt supplies the desktop tray
protocol and menu, so Facepalm only owns its status and controls.

Build and install-test the `.deb` in separate Debian 13 amd64 environments.
The package contains the tray, desktop launcher, licensed icon and replay
CLI. The MCP adapter keeps its separate Node.js 22 install because Debian
13 supplies Node.js 20. Users launch the tray from their applications menu.

Desktops with StatusNotifierItem support show the icon. The status window
covers desktops without a compatible tray. The Linux camera daemon is a
separate portability milestone. [Facepalm service issue #37](https://github.com/mattyv/facepalm/issues/37)

## 2026-11-20: Install the portable replay and MCP adapter separately

-   **Decision:** use CMake's `BUILD_TESTING` switch and install the existing
    replay CLI and MCP adapter with its lockfile under a relocatable prefix.
-   **Why:** the portable tools can be built and installed without GoogleTest
    or a camera backend, while normal test builds retain their existing suite.
-   **Rejected:** installing a synthetic fixture as a daemon or presenting the
    portable tools as a complete Linux camera application.
-   **Cost:** the adapter needs Node.js and separately installed npm
    dependencies and a separately provisioned compatible service. This does
    not deliver a Linux camera backend, daemon, model, or live camera test.
-   **Evidence:** [#37](https://github.com/mattyv/facepalm/issues/37).

## 2026-11-20: Use an ELF export list for the Linux MediaPipe shim

Replaced by "Remove the Linux ELF build of the MediaPipe shim" (2026-11-21).

-   **Decision:** build a `.so` on Linux with the same six-function C ABI as
    the macOS dylib, hiding all other symbols with an ELF version script.
-   **Why:** MediaPipe's internal symbols must not collide with another copy.
-   **Rejected:** exporting all symbols or passing Mach-O flags to Linux.
-   **Cost:** the ELF boundary was cross-linked with synthetic symbols; the
    full MediaPipe build and runtime have not yet been verified on Linux. This
    does not supply a Linux camera backend or a runnable Linux daemon.
-   **Evidence:** [#3](https://github.com/mattyv/facepalm/issues/3).

## 2026-11-20: Store feedback only when explicitly requested

Replaced by "The feeling the person picks tunes the next read, in the daemon"
(2026-11-21).

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

Its suppression rule is replaced by "A turn's suppression ends once the head
settles anywhere" (2026-11-21).

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

Its 1 to 5 rating is replaced by
"The feeling the person picks tunes the next read, in the daemon" (2026-11-21).

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

Its tuning from ratings is replaced by
"The feeling the person picks tunes the next read, in the daemon" (2026-11-21):
the person's picked feelings tune the reads.

-   **Decision:** reactions are read with rules that are the same for
    everyone, and tuned for each person from their ratings.
-   **Why:** most expressions are small, the generic readings are expected to
    be right about nine times in ten, and calibration up front is a burden.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35).
