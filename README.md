# facepalm

![A cartoon bald captain in a red tunic with his face in his palm](docs/icon.jpg)

A local perception service for tools that work alongside a programmer. It reads
the camera on the programmer's own machine and tells client tools, such as the
[AI Pair](https://github.com/mattyv/ai-pair) extension, how the programmer is
reacting: a nod, a shake, whether they are following along.

Nothing works end to end yet. The design is on
[ai-pair#83](https://github.com/mattyv/ai-pair/issues/83).

## What exists

-   `src/mediapipe_shim`: MediaPipe's face landmarker as one self-contained
    library with a plain C interface, built by `tools/fixtures/build.sh`.
    `tools/fixtures/test_shim.sh` tests it against the real model.
-   `src/head_pose`: yaw, pitch and roll in degrees from MediaPipe's facial
    transformation matrix, which is stored column by column and carries a
    uniform scale. Its test checks against MediaPipe's own expected matrix
    for its portrait test image.
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
bats scripts/test specs/test      # Tests for the scripts.
cmake --build build --target format-check
cmake -S . -B build-tidy -G Ninja -DFACEPALM_TIDY=ON \
  && cmake --build build-tidy     # clang-tidy on every source.
bash specs/check.sh               # TLC on every spec; see specs/README.md.
```

## Measuring the camera on macOS

`camera_probe` opens the camera through the controller and reports the
frames it received and the CPU time it used. The built-in camera captures at
15 to 30 frames a second at 640×480; a slower rate asks for 15. The first
run asks for camera permission for the app it runs in.

```shell
build/camera_probe 30 15  # 30 seconds at 15 frames a second.
```

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

If Xcode cannot read the macOS SDK, set `DEVELOPER_DIR` to
`/Library/Developer/CommandLineTools` first; the script passes it to Bazel.
The Fixtures workflow runs both scripts on macOS whenever
`tools/fixtures` changes.

## Licence

MIT. See [LICENSE](LICENSE).
