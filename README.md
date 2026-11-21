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
    `{"gesture":"looking_away"}` once each time the head stays away from its
    resting pose (#35): yaw or pitch more than 15 degrees from it for a
    second. `LookAwayDetector`, in `src/look_away`, is the rule; the
    resting pose is `ExpressionTracker`'s, learned from the same faces with
    the expression settings. Nods and shakes leave the resting pose for
    less than a second and are not reported. A pose held for long enough,
    under a minute, becomes the resting pose, so turning back from it reads
    as a look away of its own. `specs/LookAway.tla` models the rule.
-   `src/screen_context`, `src/screen_context_reporter`,
    `src/screen_context_reader`, `src/screen_source`: the screen context a
    reading's `context` carries (#36 item 11 of #61): the frontmost app,
    the mouse, and a coarse gaze (`this_screen`, `other_screen`, `away` or
    `unknown`) read, without calibration, from the same head pose and
    resting pose `src/look_away_reader` reads. `CoarseGazeFrom` and
    `SideOf`, in `src/screen_context`, are the rule, tested directly; it
    has no lifecycle, so no spec. `ScreenContextReader` is a `FaceReader`
    that keeps the pose behind a mutex, read from a different thread than
    it is written on. `ScreenContextReporter` builds the `context` JSON
    from `ScreenSource`, the mockable seam its tests take in place of
    `src/macos/screen_context_macos` (AppKit) and
    `src/linux/screen_context_linux` (X11, through XWayland where a
    Wayland session runs one). Window titles need macOS's Screen Recording
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
    feeling, following `specs/FeelingBeat.tla` (#70). The person can then
    say how they really felt, once per interpretation and within five
    minutes, following `specs/FeelingFeedback.tla` (#35).
-   `src/feelings`: the six feelings a reaction is read as, and the only list
    of them: agreement, disagreement, engagement, annoyance, confused and
    unknown. Its generic rule reads a beat's nods as agreement, its shakes
    as disagreement, and brows down or narrowed eyes, held, as confused;
    no cue, cues of different feelings, or a window missing a dropped
    reading read as unknown. No rule gives engagement or annoyance yet; they
    are there for corrections to name (#35). It also names what a beat
    showed, the one kind of reaction made during it and the beat's kind,
    which a correction is counted under.
-   `src/tuning`: the person's own reads (#35): how many times they picked
    each feeling for each reaction and kind of beat. A feeling picked at
    least twice and more than half the time becomes the person's read for
    it, but only once a held-out check shows it beats the generic rule: each
    pick in turn is held out and read from the others, and those reads must
    be right more often than the generic one. Three picks of the same
    feeling are enough; a split between two is not.
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
    `specs/CameraLifecycle.tla`, with the camera behind an interface.
-   `src/session_camera`: a camera over one OS's capture API: permission,
    starting, stopping and a lost device, mapped onto the spec's actions, at
    the supported frame rate nearest the one wanted. Unit-tested with the API
    mocked.
-   `src/macos/avfoundation_session`: that capture API on macOS, through
    AVFoundation, following MediaPipe's `MPPCameraInputSource`. It is the
    AVFoundation calls alone, and needs a real camera, so `camera_probe` is
    its smoke test; it has no unit tests or coverage floor.
-   `specs/CameraLifecycle.tla`: when the camera may be on, checked with TLC
    before any capture code. See `specs/README.md`.
-   `specs/FramePipeline.tla`: how a captured frame reaches the face model,
    and the model's lifecycle, checked with TLC before the pipeline's code
    (#20).
-   `specs/ReactionFeed.tla`: how readings reach each client, waits end, and
    only a reading that was shown can be rated (#36).
-   `specs/Service.tla`: the service's connections, unique client names,
    and a shutdown that cannot hang on a waiting connection (#37).

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
local camera service readiness and offline replay availability. Only one
tray runs at a time: choosing **Facepalm** again, which runs
`facepalm-menu --settings`, while one is already running reopens its
Settings form instead of opening a second tray icon (#98).
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
cmake --build build-debian --target facepalm_menu gesture_replay_cli
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
cannot tell, and `feelings`, every feeling the person can pick instead. The
feeling is advisory, never an answer. When the person says how they really
felt, `tell_feeling` takes the interpretation's ID and their pick, sends
`feel <id> <feeling>`, and Facepalm tunes later reads from it. A client resets
the person's picks with `reset feelings`.

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
# Looking away (#35): yaw or pitch this many degrees from the resting pose,
# held this long. Nods and shakes are shorter than the hold.
looking_away.enabled = true
looking_away.degrees = 15
looking_away.hold_ms = 1000
# Screen context (#36 item 11 of #61): the frontmost app, the mouse and a
# coarse gaze on every reading. Window titles can hold private text and
# need macOS's Screen Recording permission, so they default off.
screen_context.enabled = true
screen_context.window_titles_enabled = false
# Within this many degrees of the resting pose is still this_screen;
# looking_away.degrees, above, is reused as the away threshold.
screen_context.other_screen_degrees = 7.5
```

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
has what a replay needs.
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
<yaw> <pitch> <roll>` lines and prints `looking_away <timestamp>` for each
look away reported with the default settings. Probe logs from before #35
have no `head_pose` lines; `test/fixtures/README.md` shows how the three
recordings there were converted.

New probe logs include `gesture_no_face` when tracking is lost; replay resets
the window and reporting gate there. Older logs have no such markers, so their
raw timestamps alone cannot distinguish tracking loss from a dropped frame.
Replay keeps the window over a timestamp gap, as the live window does; results
for older logs may therefore differ from the original live reports. Raw values
in probe logs are rounded to six decimal places, so replay is approximate.
