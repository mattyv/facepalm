# facepalm

![A cartoon bald captain in a red tunic with his face in his palm](docs/icon.jpg)

A local perception service for tools that work alongside a programmer. It reads
the camera on the programmer's own machine and tells client tools, such as the
[AI Pair](https://github.com/mattyv/ai-pair) extension, how the programmer is
reacting: a nod, a shake, whether they are following along.

Nothing works end to end yet. The design is on
[ai-pair#83](https://github.com/mattyv/ai-pair/issues/83), and the decisions
made since, each with its reason, are in the
[decisions log](docs/decisions.md).

## What exists

-   `src/mediapipe_shim`: MediaPipe's face landmarker, as one self-contained
    library with a plain C interface, built by `tools/fixtures/build.sh`,
    on macOS. On Linux, `src/linux/mediapipe_c_shim.cc` gives its face model
    functions over MediaPipe's own `libmediapipe.so` (#65).
    `tools/fixtures/test_shim.sh` tests the shim against the real model. Its
    short-range face detector is unused since #34 replaced Nodding Pigeon
    (below); it is still built and tested, since removing it needs a Bazel
    build this environment could not run.
-   `src/face_model`: `FaceModel`, how the frame pipeline will ask for the
    face in a frame, and `MediaPipeFaceModel`, which asks the shim. Its unit
    tests stand in for the shim's C functions at link time, so they need no
    MediaPipe; `tools/face_model_check` runs it on the real model.
-   `src/frame_pipeline`: takes each captured frame to the face model, one
    frame at a time, across the capture, controller and inference queues,
    following `specs/FramePipeline.tla`. Each frame's pixels are wiped once
    it is done with.
-   `src/frame_recorder`: saves `camera_probe`'s frames on a write queue of
    their own, with a bounded backlog, and reports how many were saved,
    failed and dropped once every one is written, following
    `specs/ProbeShutdown.tla` (#41).
-   `src/frame_analyzer`, `src/face_analyzer`, `src/face_reader`: what the
    pipeline runs on each frame. `FaceAnalyzer` runs the face model once a
    frame and gives the face it found, or that there was none, to every
    `FaceReader`.
-   `src/head_pose_reader`: a `FaceReader` that reacts with the head's yaw,
    pitch and roll, and with nothing for a face whose pose is degenerate.
-   `src/gesture_reader`, `src/head_gesture_model`: a `FaceReader` that runs
    Idiap's CCDbHG head-gesture model (#34, replacing Nodding Pigeon) and
    reacts once to each nod, shake, tilt, turn or up/down. `head_gesture_model`
    is the model itself, ported from
    [idiap/ccdbhg-head-gesture-recognition](https://github.com/idiap/ccdbhg-head-gesture-recognition)
    (GPL-3.0-only); see [Licence](#licence).
-   `src/expression_reader`, `src/expressions`: a `FaceReader` that reacts
    once to each facial expression starting: brows up, a smile, pressed
    lips, brows down (a frown) or narrowed eyes (#46). `ExpressionTracker`,
    in `src/expressions`, is the mapping from MediaPipe's blendshape scores
    to expressions, judged against the person's own resting face. Its
    defaults were tuned on one person's 80-second recording; the frown
    (brows down) was the weakest of the five to read reliably. A face whose
    pose is degenerate is skipped, as a frame with no face, so that it
    cannot spoil the resting pose that gates out head movement (#79).
-   `src/look_away_reader`, `src/look_away`: a `FaceReader` that reacts
    `{"gesture":"looked_away","kind":"...","duration_ms":n}` once a look
    away ends (#35, #141): the composed gaze (head and eye together,
    #137, src/eye_gaze.h's ComposeGaze) stays away from its resting pose,
    yaw or pitch more than 15 degrees from it, for a second, and the
    report comes once it returns within half that. `LookAwayDetector`, in
    `src/look_away`, is the rule; the resting pose is `ExpressionTracker`'s,
    learned from the same faces with the expression settings. A nod or a
    shake leaves the resting pose for less than a second and is never
    reported, under any kind (a glance). The kind is `averted` (back
    within about 5 to 8 seconds), `other_screen` (the gaze landed inside
    another calibrated screen's own box while away), `away` (longer, and
    never in another screen's box) or `gone` (no face to read at some
    point during the look, read from `LookAwayReader::OnNoFace`, which
    LookAwayDetector treats as fully away from the very first missing
    frame). A pose held for long enough, under a minute, becomes the
    resting pose, so turning back from it reads as a look away of its
    own. `specs/LookAway.tla` models the rule.
-   `src/eyes_probe`, `src/eyes_replay`: the tooling half of eyes in other
    reads (#139). `FormatEyesLine`, in `src/eyes_probe`, is what
    `camera_probe` prints as its `eyes` line: the eye-look, blink, squint
    and wide blendshapes, then the iris and eye-corner landmarks, numbers
    only, alongside `head_pose` and under the same degenerate-pose skip.
    `ParseEyesLine` and `ReplayEyes`, in `src/eyes_replay`, read that line
    back into an `EyesFrame`, the way `src/gesture_replay` and
    `src/look_away_replay` read their own lines, so a later issue's eye
    reader (#140 through #145) can test against `test/fixtures/eyes`'s
    recordings. Eyes never decide a feeling by rule; gaze is context and a
    feature the personal read learns from. `scripts/record_eye_fixtures.sh`
    guides the user through recording those fixtures. `src/eyes_gaze_replay`
    replays a recording's `head_pose` and `eyes` lines together, composing
    real gaze through `ExpressionTracker`, `EyeGazeTracker` and
    `LookAwayDetector` in the order `LookAwayReader` runs them, rather than
    `src/look_away_replay`'s own head-pose-only fallback; its tests are what
    proved `eye_gaze.*`, `calibration.*` and the squint-guard's own defaults
    (below) against #139's nine fixtures. It also prints one
    `eye_sign <timestamp_ms> <blink> <eye_wide>` line per paired frame
    (#145, `src/eye_signs`'s own `SampleFrom`), which is how
    `eye_signs.*`'s own defaults (below) were measured against the same
    nine fixtures.
-   `src/eye_signs`: blink rate, a long blink and widened eyes (#145),
    coarse signs -- like `src/screen_context`'s `Gaze` -- for the
    personal read alone (`src/tuning`), never `Rule`'s own input
    (`specs/FeelingBeat.tla`'s `Interpret` is untouched). `EyeSignsOf` is
    pure computation over a window of `EyeSignSample`s a caller collects
    elsewhere; `SampleFrom` builds one sample from a frame's own `Face`,
    the same blink flag (`src/eye_gaze`'s `EyeSignalFrom`) and eyeWide
    blendshapes `src/eyes_gaze_replay`'s own `eye_sign` line reads.
    `ScreenContextReader::OnFace` builds one every live frame
    (`blink_threshold_`, read from its own `EyeGazeTracker::Settings`)
    and `ScreenContextReporter::Sample`/`EyeSignsBetween` buffer and
    read them over a beat's window (#165), the same way `Sample`/
    `Between` already do for gaze -- in the same buffer, time-bounded by
    the same `max_window_ms`, so a frame with no face samples gaze as
    `Gaze::kUnknown` but contributes nothing here, since there is no
    eyeBlink or eyeWide to read. None of #139's original nine fixtures
    carries a cue that deliberately widens the eyes, so
    `eye_signs.widened_eyes_threshold`'s own default is reasoned from
    every fixture's own eyeWide ceiling (0.051 at most), not measured
    against a true positive -- `src/eye_signs.h`'s own header comment
    says so again where the setting's default lives
    (`src/config.h`'s `kDefaultEyeSignsWidenedEyesThreshold`).
    `scripts/record_eye_fixtures.sh` grew a tenth, `widened_eyes` (#165),
    with a genuine one, for the user to record and tighten it against.
-   `src/eye_gaze`: coarse gaze reads head pose composed with eye-in-head
    signal, not head pose alone (#137). `EyeSignalFrom` reads the eye-look
    blendshapes `src/eyes_probe` already formats, into a horizontal and
    vertical signal; `ComposeGaze` adds it, gained and relative to its own
    resting level, onto head pose (head pose alone beyond
    `eye_gaze.head_only_yaw_degrees` of yaw, or with no eye signal yet).
    `EyeGazeTracker`, owned by `ScreenContextReader` alongside its
    `ExpressionTracker`, skips blinks, medians the rest over
    `eye_gaze.median_ms` and learns the resting level the way
    `ExpressionTracker` learns a resting face. `EstimateGains` is the
    calibration session's own gains step (below): the slope of head over
    eye while the person fixates and turns, refusing the step outright on
    too little head travel. A stateless computation throughout, like
    `src/screen_context`, so it is tested directly rather than modeled.
-   `src/calibration`, `src/calibration_store`, `src/calibration_reader`: an
    optional calibration session (#36, #35, item 12 of #61, #137), started,
    stepped and cancelled over the protocol's `calibrate` line: it records
    the resting head pose and eye level, a gains step (`src/eye_gaze`'s
    `EstimateGains`), and, for each screen, the composed gaze at its centre
    and four corners, then saves them to a private file the daemon owns. A
    step too weak to use -- the gains step with too little head travel, or
    a corner too close to its own screen's centre -- is refused and
    retried rather than silently accepted (#136).
    `CalibrationController`, in `src/calibration`, is the session;
    `src/calibration_store` saves and loads the file, at format version 2
    for the profile's new `resting_eye` and `gains` fields (an older
    profile fails to load, read as no profile at all);
    `src/calibration_reader` is the `FaceReader` that feeds it each
    frame's pose and eye signal, as `LookAwayReader` does its detector.
    `calibrate_cli` drives one for testing. `specs/Calibration.tla` models
    the lifecycle. A finished session's `on_finished` callback gives the
    daemon's composition root the profile at once, which calls
    `ScreenContextReporter::SetProfile` (below) so the very next reading
    already uses it, rather than only the daemon's next run (#136); the
    profile adds a `calibrated_screen` field from
    `NearestCalibratedScreen` alongside the gaze it already reads.
-   `src/camera_demand`: combines every reason the camera may be wanted
    -- an approved client subscribing, and the camera preview's own live
    request -- into the one admission signal the daemon already had
    (#176), following `specs/PreviewLease.tla`'s `Demand`.
-   `src/menu_state`: the pure decision table (#176) both the macOS menu
    and the Linux tray read for the camera-status line's title and the
    "Show Camera Preview" and "Calibrate…" items' title and enabled
    state, from whether the service is on, the camera is in use, and the
    preview is open; see the table in `docs/local-app.md`.
-   `src/preview_lease`, `src/preview_publisher`, `src/preview_request`,
    `src/macos/preview_jpeg`, `src/linux/preview_jpeg`: the camera
    preview (#36, #37, #61 item 8, #108, #114), gated so a frame is ever
    encoded only while the camera is open and the menu has asked for it
    by creating `UserPaths::preview_request` (removed to stop) -- and
    recently: a request left behind by a menu that quit without
    cleaning up, crashed, or whose session logged out, goes stale
    (`IsFreshPreviewRequest`) and stops streaming within a couple of
    seconds rather than forever. `PreviewLease`, following
    `specs/PreviewLease.tla`, is the gate: a grant never carries over a
    camera close. `PreviewPublisher` polls the request file on the
    frame-capture path already running, and while granted, encodes each
    frame to JPEG (ImageIO on macOS, libjpeg on Linux) and writes it,
    owner-only, to the private `UserPaths::preview_frame`, never to the
    approved-token service every other client reaches, clearing it once
    streaming stops. The preview is a camera demand of its own (#176,
    `src/camera_demand.h`), the same way an approved client subscribing
    already is, combined into the daemon's existing admission entry
    point rather than a second channel: opening the preview with nothing
    else using the camera turns the camera on by itself, with "Turn on
    Facepalm first" the only reason it is ever refused, that being the
    service itself not running. The menu's own "Show Camera Preview"
    window (`tools/menu/main.mm`, `tools/menu/main_linux.cc`) rereads
    that file and re-touches the request on its own timer, keeping it
    fresh for as long as the window stays open, tolerating a brief grace
    period while the daemon's own admission catches up with a request it
    just made; and removes the request outright at launch, on a clean
    quit, and when its own close button is used or the camera itself
    goes off.
-   `src/screen_context`, `src/screen_context_reporter`,
    `src/screen_context_reader`, `src/screen_source`: the screen context a
    reading's `context` carries (#36 item 11 of #61): the frontmost app,
    the mouse, and a coarse gaze (`this_screen`, `other_screen`, `away` or
    `unknown`) read from `src/eye_gaze`'s composed gaze (#137), with or
    without calibration. `CoarseGazeFrom` and `SideOf`, in
    `src/screen_context`, are the rule, tested directly; it has no
    lifecycle, so no spec. The same file's `BeatGazeName` words those four
    zones for a beat's own reply (`on_content`, `averted`, `away`,
    `unknown`, #142), `BeatGazeNamed` reads one back (#143, for
    `src/tuning`'s own counts file), and `ContentHitFrom` reads whether
    gaze was mostly inside a content rectangle a beat may carry (`yes`,
    `no`, `unknown`, #144), by linear interpolation across
    `CoarseGazeFrom`'s own uncalibrated cone -- coarse only, never angles
    or window titles (#83); also tested directly, no lifecycle of its
    own. `ScreenContextReader`
    is a `FaceReader` that keeps the pose and eye signal (its own
    `EyeGazeTracker`) behind a mutex, read from a different thread than
    they are written on, and, given one, tells an `OnSample` callback the
    same four readings every frame, plus `src/eye_signs`'s own
    `EyeSignSample` (#145/#165, `SampleFrom` read at its own
    `blink_threshold_`, nullopt with no face to read eyeBlink or eyeWide
    from), reaction or not (#142: main.cc's own hook into
    `ScreenContextReporter::Sample`, below).
    `ScreenContextReporter` composes gaze with `src/eye_gaze`'s
    `ComposeGaze` and builds the `context` JSON from `ScreenSource`, the
    mockable seam its tests take in place of
    `src/macos/screen_context_macos` (AppKit) and
    `src/linux/screen_context_linux` (X11, through XWayland where a
    Wayland session runs one); its own `Sample`/`Between` keep a short,
    time-bounded buffer of composed gaze for a beat's window to ask about
    (#142, #144), independent of whether any reading was made in it, and a
    second buffer of eye signs alongside it (`EyeSignsBetween`, #165),
    evicted by the same `max_window_ms` even on a frame that gave it
    nothing to add. Window titles need macOS's Screen Recording
    permission and can hold private text, so they default off. The
    platform sources need a real desktop session and have no unit tests or
    coverage floor, following `src/macos/avfoundation_session`'s
    precedent.
-   `src/config`: the settings file, below.
-   `src/reaction_feed`: the readings each client is told about (#36): one
    timeline of interpreted reactions, each with the summary line the person
    sees and the detail the agent gets. A client reads what came since it
    last looked, or waits for the next reading. It follows
    `specs/ReactionFeed.tla`. A client that has just asked a question or
    made a proposal can instead mark a beat: the feed waits out the beat's
    window and answers with the readings and one interpretation of them, a
    feeling, following `specs/FeelingBeat.tla` (#70). The interpretation
    also carries the majority gaze over that window as context (#142,
    `ScreenContextReporter::Between`, injected as `GazeWindowFn`), and,
    given a content rectangle with the beat, whether gaze was mostly
    inside it (#144); gaze never changes the feeling by rule, the one
    exception being `GuardAvertedUnknown`, the thinking guard: a question
    beat ending averted reads unknown however the cues went, so the client
    waits rather than acts on a guess. The same window's blink rate, long
    blink and widened eyes (#145) are read the same way
    (`ScreenContextReporter::EyeSignsBetween`, injected as
    `EyeSignsWindowFn`, #165) and folded into the evidence alongside gaze,
    but never shown on the reply at all: a feature the personal read
    alone weighs, so there is no guard to write for them. The person can
    then say how they really felt, once per interpretation and within
    five minutes, following `specs/FeelingFeedback.tla` (#35).
-   `src/feelings`: the six feelings a reaction is read as, and the only list
    of them: agreement, disagreement, engagement, annoyance, confused and
    unknown. Its generic rule reads a beat's nods as agreement, its shakes
    as disagreement, and brows down or narrowed eyes, held, as confused;
    no cue, cues of different feelings, or a window missing a dropped
    reading read as unknown. No rule gives engagement or annoyance yet; they
    are there for corrections to name (#35). It also names what a beat
    showed, the one kind of reaction made during it, the beat's kind,
    where the person was looking during it, in the beat's own wording
    (#143, `src/screen_context.h`'s `Gaze` and `BeatGazeName`), and, added
    by #145, blink rate, a long blink and widened eyes
    (`src/eye_signs.h`'s `BlinkRate`, `LongBlink` and `WidenedEyes`) --
    every one of these a feature the person's own read (`src/tuning`)
    learns from, never `Rule`'s own input (#142's
    `ContextNeverChangesFeeling`, untouched by #145 too); `EvidenceOf`
    itself leaves all four at their own unknown value, for a later
    issue's own window reader to fill in the way `src/reaction_feed`'s
    `Beat` already does for gaze.
-   `src/tuning`: the person's own reads (#35, gaze added by #143; blink
    rate, a long blink and widened eyes added by #145): how many times
    they picked each feeling for each reaction, kind of beat, and the
    four context fields during it. A feeling picked at least twice and
    more than half the time becomes the person's read for it, but only
    once a held-out check shows it beats the generic rule: each pick in
    turn is held out and read from the others, and those reads must be
    right more often than the generic one. Three picks of the same
    feeling are enough; a split between two is not. Below that minimum
    for the evidence's own exact bucket (sign, beat, gaze, blink rate,
    long blink and widened eyes all matching), the read instead runs over
    every pick of the same reaction and beat, whatever the other three
    fields were made under, so a feeling no generic rule gives
    (engagement, annoyance) can still come from the person's labels
    before enough of them share one exact bucket. The counts file's own
    header bumped from `facepalm feelings 1` to `facepalm feelings 2` for
    gaze (#143), and now to `facepalm feelings 3` for the three new words
    (#145); unlike a calibration profile's own version bump
    (`src/calibration_store`, #137), a v1 or v2 file is read, not
    refused -- each of a v1 file's picks reads with gaze, blink rate,
    long blink and widened eyes all unknown, and a v2 file's with the
    three new fields unknown, gaze kept; the next save always writes the
    file back as v3, so the person's own picks are never thrown away for
    a file they never asked to change.
-   `src/feeling_store`: keeps those counts in one file private to the
    person, `facepalm/feelings` in `$XDG_CONFIG_HOME` or `~/.config` unless
    the daemon is given `--feelings-file PATH`. It is the file's only
    writer: it holds `feelings.lock` while open, so a second daemon cannot
    open it, and saves one pick at a time by renaming a synced temporary
    file over it, so a crash leaves the old counts or the new. A pick is
    answered ok only once the folder is synced too.
-   `src/service`: the background service's connections (#37), following
    `specs/Service.tla`. Each connection names its client, then reads,
    waits for a reading, marks a beat, says how the person felt at one, or
    forgets every feeling the person gave, in the protocol of
    `src/protocol`: a request is a line of text, and each reply a line of
    JSON. Stopping ends every connection, even one waiting.
-   `src/unix_socket`: the service's connections over a Unix domain socket
    that only its user can open, on macOS and Linux; tested on real
    sockets.
-   `src/reaction`: the reaction to one face, its head's yaw, pitch and roll
    to a tenth of a degree, as JSON text; none for a degenerate pose matrix,
    such as all zeros or one holding NaN or infinity, whose angles JSON
    cannot hold (#44).
-   `src/head_pose`: yaw, pitch and roll in degrees from MediaPipe's facial
    transformation matrix, which is stored column by column and carries a
    uniform scale. Its test checks against MediaPipe's own expected matrix
    for its portrait test image.
-   `src/frame`: a captured frame with its capture time in milliseconds, and
    the one-pass copy from the camera's padded BGRA rows to the packed RGBA
    MediaPipe accepts.
-   `src/camera_controller`: decides when the camera may be on, following
    `specs/CameraLifecycle.tla`, with the camera behind an interface. A
    denied permission or a failed start (#52, item 10 of #61) schedules its
    own retry, through an injected `RetrySchedule`, rather than waiting for
    a device-connected report the way a missing camera does.
-   `src/session_camera`: a camera over one OS's capture API: permission,
    starting, stopping and a lost device, mapped onto the spec's actions, at
    the rate nearest the one wanted from the device's own frame-rate ranges,
    kept apart rather than collapsed into one minimum and maximum (#43).
    Unit-tested with the API mocked.
-   `src/camera_directory`: `CameraDevice{id, name, built_in}`, what
    `CaptureSession::List` reports for choosing the camera (item 10 of #61,
    #13): `id` is stable across a reboot, and the protocol's `cameras` line
    answers with it.
-   `src/macos/avfoundation_session`: that capture API on macOS, through
    AVFoundation, following MediaPipe's `MPPCameraInputSource`. `Configure`
    resolves `Config`'s `camera.device_id`: blank for the first built-in
    camera an `AVCaptureDeviceDiscoverySession` finds, by its own device
    type, never whatever `+defaultDeviceWithMediaType:` hands back, which
    could be a Continuity Camera; a chosen id is matched the same way,
    across every discoverable type. It is the AVFoundation calls alone, and
    needs a real camera, so `camera_probe` is its smoke test; it has no
    unit tests or coverage floor.
-   `specs/CameraLifecycle.tla`: when the camera may be on, checked with TLC
    before any capture code. See `specs/README.md`.
-   `specs/FramePipeline.tla`: how a captured frame reaches the face model,
    and the model's lifecycle, checked with TLC before the pipeline's code
    (#20).
-   `specs/ReactionFeed.tla`: how readings reach each client, waits end, and
    only a reading that was shown can be rated (#36).
-   `specs/Service.tla`: the service's connections, unique client names,
    and a shutdown that cannot hang on a waiting connection (#37).
-   `specs/Calibration.tla`: the optional calibration session's lifecycle,
    including the camera going away mid-session, checked with TLC before
    the session's code (#36, #35, item 12 of #61).

## Build and test

Tests require CMake 3.28 or later, Ninja, a C++20 compiler, and GoogleTest. On
macOS, `brew install cmake ninja googletest llvm bats-core shellcheck shfmt
jq`.

```shell
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Checks that CI runs:

```shell
bash scripts/coverage.sh          # Each source's own tests, against its floors.
# On Linux, the camera's sources in src/linux and the daemon too (#83).
bash scripts/fetch_mediapipe_c.sh build-mediapipe-c
bash scripts/coverage.sh \
  -DFACEPALM_MEDIAPIPE_C_INCLUDE="$PWD/build-mediapipe-c/include" \
  -DFACEPALM_MEDIAPIPE_C_LIBRARY="$PWD/build-mediapipe-c/lib/libmediapipe.so"
# On Linux with Clang: check memory errors and undefined behavior.
CXX=clang++ cmake -S . -B build-sanitizers -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
cmake --build build-sanitizers && ctest --test-dir build-sanitizers --output-on-failure --no-tests=error
# Data races, in a build of its own: ThreadSanitizer cannot share one with
# AddressSanitizer. Clang on macOS runs it too.
CXX=clang++ cmake -S . -B build-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
cmake --build build-tsan && TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan --output-on-failure --no-tests=error
bats scripts/test specs/test tools/fixtures/test  # Tests for the scripts.
bash scripts/check_bats.sh        # Every bats assertion ends in || false.
# On Linux with kcov: every shell script's lines, at 100 percent.
bash scripts/coverage_shell.sh
cmake --build build --target format-check
cmake -S . -B build-tidy -G Ninja -DFACEPALM_TIDY=ON \
  && cmake --build build-tidy     # clang-tidy on every source.
bash specs/check.sh               # TLC on every spec; see specs/README.md.
```

### Fuzz and property tests

`FACEPALM_FUZZTEST` (off by default) fetches [Google
FuzzTest](https://github.com/google/fuzztest) and builds `test/fuzz/`: the
parsers that read untrusted input (`src/protocol.cc`'s hello line first,
since it reaches unauthenticated local processes before any approval, then
config, settings, the feelings counts file, frame times, the calibration
profile and the `cameras` listing) fuzzed, and the pure rules (`Interpret`,
`Tuning`, the frame-rate choice, the settings document, head pose and
protocol's own round trip) property-tested, with a seed corpus in
`test/fuzz/corpus/`. Each is a GoogleTest executable, `<name>_fuzz`, that
runs normally (`ctest`, a plain `cmake --build`) too, briefly and without
instrumentation:

```shell
cmake -S . -B build-fuzz -G Ninja -DFACEPALM_FUZZTEST=ON
cmake --build build-fuzz
ctest --test-dir build-fuzz -R '_fuzz$'
```

Real, continuous, coverage-guided fuzzing needs Clang's sanitizer coverage
instrumentation (`-DFUZZTEST_FUZZING_MODE=on`), verified on Linux in CI's
`fuzzing` job; it may not work on macOS, so a local check there stays in the
brief mode above:

```shell
CXX=clang++ cmake -S . -B build-fuzzing -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DFACEPALM_FUZZTEST=ON -DFUZZTEST_FUZZING_MODE=on \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
cmake --build build-fuzzing
build-fuzzing/protocol_fuzz --fuzz_for=45s   # Every FUZZ_TEST in the binary.
```

`<name>_fuzz` is exempt from `coverage_thresholds.json`'s floors, the way
`menu_store_checks` and `preview_jpeg_checks` are: it tests a property over
many generated inputs, not the lines a fixed set of examples exercises.

`coverage_shell.sh` runs every bats test under kcov and holds each shell
script in git to 100 percent of its lines, its `shell` floor in
`coverage_thresholds.json`; it names the lines a script's tests miss. It runs
on Linux only, in CI's `shell-coverage` job: on macOS, kcov's bash tracing
rides on stderr, which bats captures, so every script would read 0. kcov
counts some lines bash never traces, so the scripts avoid them: an awk
program is read from a here-document, a long list is built up with `+=` one
line at a time, a `$(...)` stays on one line (its arguments in an array when
they are long), and a `while read` loop reads a here-string, not
`done <file`. A command continued with a trailing backslash is fine: kcov
counts only its first line.

See [local macOS app staging](docs/local-app.md) for the development installer.

### Linux desktop tray and Debian package

The Debian 13 amd64 package installs the Facepalm 🤦 tray app and the
`gesture_replay` CLI. Launch **Facepalm** from the desktop applications menu.
The tray is displayed by desktops with StatusNotifierItem support. GNOME
may need the [AppIndicator support extension](https://extensions.gnome.org/extension/615/appindicator-support/).
The applications menu opens **Facepalm Settings** for frame rate, gesture
and expression detection, and advanced tuning. Choose **Settings…** from the
tray to reopen the form. **Turn On Facepalm…** in the tray, as in the macOS
menu, gives consent and starts the camera daemon as a systemd user service
with the saved settings; **Turn Off Facepalm** stops it. The tray also shows
local camera service readiness, offline replay availability, and a
**Camera on**/**Camera off** indicator that follows a client's
subscription within a couple of seconds (#61 item 8). Only one tray
runs at a time: choosing **Facepalm** again, which runs
`facepalm-menu --settings`, while one is already running reopens its
Settings form instead of opening a second tray icon (#98). **Show
Camera Preview** opens a window of the camera, started only by hand and
turning the camera on by itself if nothing else has it on already
(#176), and stopped from the same item, by the window's own close
button, or when the camera goes off (#108); quitting the tray, or an
earlier run that crashed or was logged out mid-preview, ends it too
(#114). While Facepalm is off, **Show Camera Preview** and
**Calibrate…** are disabled, titled "Turn on Facepalm first" (#176).
For live camera capture, use the separate [Linux daemon build](docs/linux-camera.md)
or the macOS daemon.

Download `facepalm-debian13-amd64` from the Facepalm CI run, extract its
`.deb`, and install the file with `apt`.

```shell
sudo apt install ./facepalm_0.1.0_amd64.deb
```

For a local Debian 13 build, install `qt6-base-dev`, `qt6-wayland`, and
`dpkg-dev` along with the compiler, CMake and Ninja. Then configure, build
and package the desktop tools.

```shell
cmake -S . -B build-debian -G Ninja -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release -DFACEPALM_LINUX_TRAY=ON
cmake --build build-debian --target facepalm_menu gesture_replay_cli calibrate_cli_tool
cpack --config build-debian/CPackConfig.cmake -B build-debian/package
```

The separately installed MCP adapter uses the Node.js setup below.

### Portable replay and MCP adapter install

The portable build installs the offline gesture replay CLI and the existing
Node MCP adapter, plus licence and third-party notices. For live capture,
build the [Linux camera daemon](docs/linux-camera.md) or stage the macOS
app. The adapter connects to a separately running compatible service.
With testing disabled, the build needs CMake 3.28 or newer, Ninja, and a
C++20 compiler.

```shell
cmake -S . -B build-portable -G Ninja -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/facepalm"
cmake --build build-portable
cmake --install build-portable
"$HOME/.local/facepalm/bin/gesture_replay" < camera-probe.log
cd "$HOME/.local/facepalm/share/facepalm/mcp"
npm ci --omit=dev
node main.js
```

The MCP adapter needs Node.js 22 or newer and the lockfile dependencies.
`npm ci` obtains those packages from a registry or an existing npm cache;
CMake installs the source and lockfile without `node_modules`. With the default
relative install paths, the installed tree can be moved as a unit. Given no
arguments, the adapter connects to the daemon's socket with the token
`facepalm-daemon approve mcp` made, both in the per-user folder:
`~/Library/Application Support/Facepalm` on macOS, `~/.local/state/facepalm`
on Linux. `node main.js SOCKET --client-token-file PATH` names them instead.

The adapter's `wait_for_reaction` tool returns the readings since the session
last looked. Right after the agent asks the person something, it can pass
`beat` (`question` or `proposal`): the service then watches the next
`timeout_ms` (1500 by default, at most 10000) and adds `interpretation`, with
an ID, the feeling the reaction was read as, which is `unknown` when it
cannot tell, and `feelings`, every feeling the person can pick instead.
`interpretation.gaze` is the majority gaze over that window as context
(`on_content`, `averted`, `away` or `unknown`, #142): it never changes the
feeling by rule, the one exception being a question beat ending averted,
which reads unknown so the agent waits rather than acts on a guess.
Passing `content_rect`, the screen area the question or proposal occupies,
with `beat` also adds `interpretation.content` (#144): whether gaze was
mostly inside it, `yes`, `no` or `unknown`, coarse only -- never angles or
window titles (#83). A pick the person later makes replaces this read for
that beat; with no pick, the
read stands, and nothing waits on one. The feeling is advisory either way,
never an answer. When the person says how they really felt, `tell_feeling`
takes the interpretation's ID and their pick, sends `feel <id> <feeling>`,
and Facepalm tunes later reads from it. `tell_feeling`, `ask_feeling` (below)
and `get_feeling` all return `{id, read, picked, feeling}`: `feeling` is
`picked` once there is one, otherwise `read`. A pick Facepalm itself refuses
(already given, or more than five minutes old) still carries `picked`, since
the person did pick, even though it will not tune from it; a call that never
reaches a definite yes or no from Facepalm at all (offline, unapproved, or an
unrecognized ID) leaves the read standing instead. `get_feeling` looks up
which feeling now stands for an ID with no call of its own, for a host that
does not otherwise surface a tool's own result back to the agent. A client
resets the person's picks with `reset feelings`. `open_facepalm_settings`
requests the installed Facepalm Settings window (#61 item 8); it does not
start the camera, change consent, or confirm the window became visible.

Where the host renders interactive widgets ([MCP
Apps](https://github.com/modelcontextprotocol/ext-apps)), `show_feeling_card`
shows the person a "How were you feeling just then?" card for a beat's
interpretation: one button per feeling, in the order `feelings` lists them,
Facepalm's own read already highlighted, and one click either way, which
sends `tell_feeling` itself. The card never names the gesture, the
interpretation's ID, or any basis, eligibility or retention detail; a second
click is refused. Once Facepalm takes the pick, the card passes
`tell_feeling`'s own result to the host through `app.updateModelContext()`,
where the host supports it, so the agent learns the pick even when the host
never surfaces a tool call's own result back to it; `get_feeling` is the
fallback elsewhere. A host without MCP Apps gets the tool's text instead,
with the same `id`, `feeling` and `feelings` to act on directly. See [local
macOS app staging](docs/local-app.md) for the Claude Desktop configuration
that renders the card.

Where the host cannot render a widget but supports MCP elicitation, such as
the Claude Code terminal, `ask_feeling` asks the same question as a form: one
field, an enum of `feelings` with the same plain-word names, Facepalm's own
read as the default. An accepted pick is sent on as `tell_feeling` already
is; a decline, a cancel, or a host that refuses to elicit is reported without
an error, the read left standing. For Claude Code, `/facepalm:feel` calls it
with the most recent beat's interpretation when given no arguments.

To test the install and relocation without GoogleTest, pass a new, disposable
path as `SMOKE_ROOT`; the test removes it afterwards. To also exercise the
installed adapter against the existing test-only service, first install the
adapter dependencies and build the fixture using the testing build above.

```shell
(cd mcp && npm ci)
cmake --build build --target mcp_service_fixture
cmake -DSOURCE_DIR="$PWD" -DSMOKE_ROOT=/tmp/facepalm-install-smoke \
  -DMCP_NODE_MODULES="$PWD/mcp/node_modules" \
  -DMCP_FIXTURE="$PWD/build/mcp_service_fixture" -P test/install_smoke.cmake
```

Omit both MCP arguments to check the replay install alone. The fixture supplies
a test feed over a local socket; it does not exercise a camera.

The MCP integration test uses a synthetic feed and the real local socket; it
requires Node.js 22 and the MCP adapter dependencies, but no camera:

```shell
(cd mcp && npm ci)
cmake --build build --target mcp_service_fixture
node --test test/integration/*.test.mjs
```

## Settings

facepalm reads `~/.config/facepalm/facepalm.conf`, or
`$XDG_CONFIG_HOME/facepalm/facepalm.conf` when that is set. Every setting has
a default, so the file holds only what you change; with no file, all are
defaults. A misspelt name, a bad value or a repeated setting stops facepalm
with the line named.

```text
# The camera's frame rate: 15 by default, 30 for quicker gestures.
camera.frames_per_second = 15
# Which camera to use, by its stable ID (item 10 of #61, #13): blank for
# the default, the built-in camera, never a Continuity Camera or other
# external device chosen silently. The protocol's `cameras` line lists
# every camera's id, name and whether it is built in; a changed choice
# takes effect on the daemon's next start.
camera.device_id =
# The head's yaw, pitch and roll on every frame.
head_pose.enabled = true
# Nods, shakes, tilts, turns and up/downs: how sure the model must be of
# its most likely class before it is reported.
gestures.enabled = true
gestures.confidence_threshold = 0.5
# Brows up, a smile, pressed lips, brows down (a frown) and narrowed eyes.
expressions.enabled = true
# The smallest rise above the person's own resting face that counts.
expressions.smallest_rise = 0.06
# How many times the resting face's own wobble a rise must also exceed.
expressions.spreads = 3
# How long a rise must last before it counts, so a blink is not a squint.
expressions.hold_ms = 200
# How far the head may lean from its resting pose, in degrees, and how much
# it may move within the last 500 ms, before expressions pause (#46).
expressions.pose_degrees = 10
expressions.pose_range = 3
# How long after the head was last away or moving it keeps pausing.
expressions.settle_ms = 600
# Narrowed eyes alone pauses further on a downward glance or a blink,
# which lift the squint score the same way a real squint does (#140):
# MediaPipe's eyeLookDown and eyeBlink scores, either eye, above which it
# pauses. Tuned on #139's recordings.
expressions.eye_look_down_threshold = 0.2
expressions.eye_blink_threshold = 0.5
# Looking away (#35): yaw or pitch this many degrees from the resting pose,
# held this long. Nods and shakes are shorter than the hold.
looking_away.enabled = true
looking_away.degrees = 15
looking_away.hold_ms = 1000
# Back within this long of first passing looking_away.degrees reads as
# "averted" rather than "away" (#141, provisional: 5 to 8 seconds).
looking_away.averted_ms = 6500
# How far outside a calibrated screen's own box still counts as that
# screen, for the "other_screen" kind (#141, provisional).
looking_away.other_screen_margin_degrees = 3
# Screen context (#36 item 11 of #61): the frontmost app, the mouse and a
# coarse gaze on every reading. Window titles can hold private text and
# need macOS's Screen Recording permission, so they default off.
screen_context.enabled = true
screen_context.window_titles_enabled = false
# Within this many degrees of the resting pose is still this_screen;
# looking_away.degrees, above, is reused as the away threshold.
screen_context.other_screen_degrees = 7.5
```

The macOS menu's and the Linux tray's **Settings** windows show and save
most of the settings above, including looking away's own `enabled`,
`degrees` and `hold_ms` (#79), screen context's (window titles still
default off there too) and the squint-guard's own `eye_look_down_threshold`
and `eye_blink_threshold` (#140, #150). `eye_gaze.*`, `eye_signs.*`
(#145), `calibration.*` (#137) and looking away's own `averted_ms` and
`other_screen_margin_degrees` (#141) are config-file only, provisional
settings not yet shown there. Turn Facepalm off and on again from the menu
to apply a change, as for the other settings.

A nod, a shake or a tilt moves the blendshape scores as much as an
expression does, so while the head is away from its resting pose, or has
moved more than a little recently, facepalm reads those scores as pose, not
face: it neither reports an expression from them nor learns the resting
face from them. A posture that settles somewhere new becomes the resting
pose, and the face it settles with is learned afresh as the resting face:
the scores shift with the posture, and a shift held through the whole
movement is the new posture's face, not an expression made at its end.
The trade-off is that an expression made while the head
moves is reported only if it is still held once the head settles again, a
little after the movement stops.

## Screen context

With `screen_context.enabled`, every reading's `context` object carries what
the screen showed at that moment: `active_app` (the frontmost app's name),
`window_title` (only with `screen_context.window_titles_enabled`), `mouse`
(`x`/`y`), and `gaze` (`this_screen`, `other_screen`, `away` or `unknown`).
Nothing else about the screen, and no image of it, leaves the process.

Gaze is read from the same head pose and resting pose `looking_away` reads,
against the screens the platform reports, with no calibration to the
camera's own position: a screen is placed only to the left or the right of
the one the camera is assumed to face, by `screen_context.other_screen_degrees`
and `looking_away.degrees`. `away` and `other_screen` otherwise mean the same
things `looking_away` already says about head pose; this only reads it as a
snapshot, not a held event.

macOS reads the frontmost app, the mouse and the screens through AppKit,
which needs no permission; a window's title needs the Screen Recording
permission and reads as absent without it.

Linux reads the same through X11, reached through XWayland where a Wayland
session runs one. Verified, not assumed: under a GNOME Wayland session,
Mutter keeps a Wayland client's real pointer position and window focus from
X11 clients, and XWayland sets `_NET_ACTIVE_WINDOW` to `None` once a native
Wayland window has focus. So the mouse and the active app read as absent
whenever `WAYLAND_DISPLAY` is set, rather than report a stale or partial
answer; the screens' arrangement, a separate subsystem XWayland still
forwards, is read either way. The Linux build needs `libx11-dev` and
`libxrandr-dev`.

## Measuring the camera on macOS

`camera_probe` opens the camera through the controller, converts each frame
to RGBA as the face model will, and reports the frames it received, the CPU
time it used, and how many capture times failed to increase. The built-in
camera captures at 15 to 30 frames a second at 640×480; a slower rate asks
for 15. The first run asks for camera permission for the app it runs in.

```shell
build/camera_probe 30 15  # 30 seconds at 15 frames a second.
```

A reported head turn stays latched until the face returns within ten degrees
of its estimated starting yaw and the model reads no gesture for 750 ms, or
until the model reads no gesture for 2 seconds with the face within ten
degrees of wherever it settled (#62). The estimate is the oldest yaw in the
one-second model window when turn classification starts. A glance and its
return are one turn; a head that settles on a second screen gets turns again,
and turning back from it is reported as a turn of its own. Tracking loss
resets the latch; it does not establish that the head returned. The ten-degree
tolerance is based on one recording and may not fit everyone. A turn is head
movement, not an inference about gaze or attention. Other gesture classes keep
their existing reporting rule.

Every frame goes through the frame pipeline, with the settings above; a
frame rate given on the command line wins. Built with
`FACEPALM_MEDIAPIPE_DYLIB` (see below) and given the face landmarker's
`.task` bundle as a third argument, it runs the face model once a frame and
reacts with each setting enabled: the head's angles as
`reaction {"yaw":2.4,"pitch":-3.2,"roll":-0.5}`, each facial expression
(once it starts) as `reaction {"expression":"smile"}`, and each nod, shake,
tilt, turn or up/down as `reaction {"gesture":"nod"}`, once the model has held
that class continuously for 200ms; it then stays silent, whatever the model
says next, until the model has held still (`none`) continuously for 750ms,
so one physical movement is one reaction even where the model's per-frame
answer flickers between two related classes or to `none` and back (#34).
It reports how long each detect took, how many frames had a face, the
thread count and peak memory. Given a directory as a fourth argument, it
saves every frame there as a PNG, with each frame's capture time in
`timestamps.txt`, for `face_landmarker_fixtures` to replay. At most 30
frames wait to be written; a frame past that is dropped. The probe waits
for every frame to be written before it exits, prints `recorded_frames`,
`recording_failed` and `recording_dropped`, and exits with 1 when any
frame is missing from the recording (#41). Those frames
are images of whoever sat at the camera: keep them out of the repository.
Replay them with `face_landmarker_fixtures --recording
<directory>/timestamps.txt <model> <directory>/frame_*.png`, which runs each
frame at its own capture time, so a 15 fps recording plays at its own speed
and a dropped frame keeps its gap (#42); it refuses a frame with no line in
`timestamps.txt`, and times that do not increase. `--video` instead runs
images 33 ms apart.
Every frame with a face also prints its gesture channels as
`gesture_raw <timestamp_ms> <18 values>` (head_gesture::RawFrame, the model's
own input), and once that frame's window is ready, its class probabilities
as `gesture_probs <timestamp_ms> <6 values>` (none, nod, shake, tilt, turn,
up/down), whether or not `gestures.enabled` is set, so a recording always
has what a replay needs. A frame whose pose is not degenerate also prints
its head pose as `head_pose <timestamp_ms> <yaw> <pitch> <roll>` and,
alongside it, its eyes as `eyes <timestamp_ms> <32 values>` (#139): the
eye-look, blink, squint and wide blendshapes, then the iris and eye-corner
landmarks, in `src/eyes_probe.h`'s order. `src/eyes_replay.h` reads that
line back; [test/fixtures/eyes's README](test/fixtures/eyes/README.md)
covers recording fixtures with `scripts/record_eye_fixtures.sh`.
`face_model_check <model> <image> <frames>` times the model on one image run
as that many frames, for a face held still in front of the camera.
`face_model_check --gesture <model> <frame.png>...` runs the head-gesture
model over those images as frames of one video, 33 ms apart, and prints the
last window's gesture and probabilities; with `--recording <timestamps.txt>`
before the model, it runs a recording's frames at their capture times
instead.

For energy, run `powermetrics` alongside it in another terminal, and read
`camera_probe`'s line:

```shell
sudo powermetrics --samplers tasks --show-process-energy -i 1000 -n 30
```

## Golden fixtures from MediaPipe

`tools/fixtures` builds MediaPipe's own C++ face landmarker, pinned to release
v0.10.35, and a small tool that writes its landmarks, blendshapes and
transformation matrix for each image as JSON. These are the reference that
facepalm's own face pipeline is tested against. Building it needs Bazel
(`brew install bazelisk`) and takes ten minutes or more the first time;
everything it fetches and builds goes in `build-mediapipe/`. The tool reads a
recording's `timestamps.txt` with `src/frame_times.cc`, which facepalm's own
build unit-tests; `build.sh` links `src` into MediaPipe's workspace as the
package `//src` for it.

```shell
bash tools/fixtures/build.sh
bash tools/fixtures/check_portrait.sh  # Matches MediaPipe's expected results.
build-mediapipe/mediapipe/bazel-bin/facepalm_fixtures/face_landmarker_fixtures \
  build-mediapipe/data/face_landmarker_v2_with_blendshapes.task image.jpg
```

`face_model_check` is facepalm's own `MediaPipeFaceModel` over the shim that
build made. `check_face_model.sh` requires its results to match the fixture
tool's exactly, on a lossless PNG of the portrait so both read the same
pixels:

```shell
shim="${PWD}/build-mediapipe/mediapipe/bazel-bin/facepalm_shim"
cmake -S . -B build -G Ninja \
  -DFACEPALM_MEDIAPIPE_DYLIB="${shim}/libfacepalm_mediapipe.dylib"
cmake --build build --target face_model_check
bash tools/fixtures/check_face_model.sh build/face_model_check
```

If Xcode cannot read the macOS SDK, set `DEVELOPER_DIR` to
`/Library/Developer/CommandLineTools` first; the script passes it to Bazel.
The Fixtures workflow runs these checks on macOS whenever `tools/fixtures`,
`src/mediapipe_shim`, `src/face_model` or `tools/face_model_check` changes,
and keeps Bazel's compiled outputs between runs, so a warm run builds only
what changed.

## Licence

GPL-3.0-only. See [LICENSE](LICENSE). facepalm was MIT before #34 ported
Idiap's CCDbHG head-gesture model, which is GPL-3.0-only; see
[THIRDPARTY.md](THIRDPARTY.md) for the model's own notice.

### Replay head gestures offline

Build `gesture_replay` with `cmake --build build --target gesture_replay_cli`, then
run `build/gesture_replay < camera-probe.log`. Each ready window prints its
timestamp, six model probabilities, classified gesture and reported gesture.
The class and report numbers are 0 none, 1 nod, 2 shake, 3 tilt, 4 turn and
5 up/down. The default confidence threshold is used.

Run `build/gesture_replay --looking-away < camera-probe.log` to replay a
recording's looks away instead: it reads the probe's `head_pose <timestamp>
<yaw> <pitch> <roll>` lines (no eyes line, so gaze is head pose alone, the
fallback #137 uses with no eye signal) and its `gesture_no_face` lines
(#141's own `gone` kind), and prints
`looked_away <timestamp> <kind> <duration_ms>` for each look away reported
with the default settings, once it returns. Probe logs from before #35
have no `head_pose` lines; `test/fixtures/README.md` shows how the three
recordings there were converted.

New probe logs include `gesture_no_face` when tracking is lost; replay resets
the window and reporting gate there. Older logs have no such markers, so their
raw timestamps alone cannot distinguish tracking loss from a dropped frame.
Replay keeps the window over a timestamp gap, as the live window does; results
for older logs may therefore differ from the original live reports. Raw values
in probe logs are rounded to six decimal places, so replay is approximate.
