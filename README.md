# facepalm

![A cartoon bald captain in a red tunic with his face in his palm](docs/icon.jpg)

A local perception service for tools that work alongside a programmer. It reads
the camera on the programmer's own machine and tells client tools, such as the
[AI Pair](https://github.com/mattyv/ai-pair) extension, how the programmer is
reacting: a nod, a shake, whether they are following along.

Nothing works end to end yet. The design is on
[ai-pair#83](https://github.com/mattyv/ai-pair/issues/83).

## What exists

-   `src/mediapipe_shim`: MediaPipe's face landmarker, and its short-range
    face detector for Nodding Pigeon (#34), as one self-contained library with
    a plain C interface, built by `tools/fixtures/build.sh`.
    `tools/fixtures/test_shim.sh` tests it against the real model.
-   `src/face_model`: `FaceModel`, how the frame pipeline will ask for the
    face in a frame, and `MediaPipeFaceModel`, which asks the shim. Its unit
    tests stand in for the shim's C functions at link time, so they need no
    MediaPipe; `tools/face_model_check` runs it on the real model.
-   `src/frame_pipeline`: takes each captured frame to the face model, one
    frame at a time, across the capture, controller and inference queues,
    following `specs/FramePipeline.tla`. Each frame's pixels are wiped once
    it is done with.
-   `src/frame_analyzer`, `src/head_pose_analyzer`, `src/gesture_analyzer`:
    what the pipeline runs on each frame. `HeadPoseAnalyzer` reacts with the
    head's angles; `GestureAnalyzer` runs Nodding Pigeon and reacts once to
    each nod or shake.
-   `src/nodding_pigeon`, `src/face_detector`: Nodding Pigeon's head gesture
    model (MIT, weights v0.1.0) ported to C++, and the short-range face
    detector it reads. On its own test videos the port reads nodding, turning
    and stationary as expected; the fake turn, four still photos, reads
    nodding (#34).
-   `src/config`: the settings file, below.
-   `src/reaction`: the reaction to one face, its head's yaw, pitch and roll
    to a tenth of a degree, as JSON text.
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

## Build and test

Requires CMake 3.28 or later, Ninja, a C++20 compiler, and GoogleTest. On
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
bats scripts/test specs/test tools/fixtures/test  # Tests for the scripts.
bash scripts/check_bats.sh        # Every bats assertion ends in || false.
cmake --build build --target format-check
cmake -S . -B build-tidy -G Ninja -DFACEPALM_TIDY=ON \
  && cmake --build build-tidy     # clang-tidy on every source.
bash specs/check.sh               # TLC on every spec; see specs/README.md.
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
# Nods and shakes, with Nodding Pigeon's thresholds: how sure it must be
# that the head moves, and that the movement is a nod or a shake.
gestures.enabled = true
gestures.motion_threshold = 0.5
gestures.gesture_threshold = 0.9
```

## Measuring the camera on macOS

`camera_probe` opens the camera through the controller, converts each frame
to RGBA as the face model will, and reports the frames it received, the CPU
time it used, and how many capture times failed to increase. The built-in
camera captures at 15 to 30 frames a second at 640×480; a slower rate asks
for 15. The first run asks for camera permission for the app it runs in.

```shell
build/camera_probe 30 15  # 30 seconds at 15 frames a second.
```

Every frame goes through the frame pipeline, with the settings above; a
frame rate given on the command line wins. Built with
`FACEPALM_MEDIAPIPE_DYLIB` (see below) and given the face landmarker's
`.task` bundle as a third argument, it also runs the face model on every
frame, prints each frame's reaction as a line such as
`reaction {"yaw":2.4,"pitch":-3.2,"roll":-0.5}`, and, with
`face_detection_short_range.tflite` beside the bundle, each nod or shake as
`reaction {"gesture":"nod"}`. It reports how long each
detect took, how many frames had a face, the thread count and peak memory.
Given a directory as a fourth argument, it saves every frame there as a PNG,
with each frame's capture time in `timestamps.txt`, for
`face_landmarker_fixtures` to replay. Those frames are images of whoever sat
at the camera: keep them out of the repository.
`face_model_check <model> <image> <frames>` times the model on one image run
as that many frames, for a face held still in front of the camera.

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
everything it fetches and builds goes in `build-mediapipe/`.

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

MIT. See [LICENSE](LICENSE).
