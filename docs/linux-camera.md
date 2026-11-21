# Linux camera daemon development build

Facepalm uses the existing consent, camera, analysis, and MCP service code
with a single-plane Video4Linux device on Linux. The default camera is
`/dev/video0`. Set `FACEPALM_CAMERA_DEVICE` to select another device. The
Debian desktop package contains the tray app and gesture replay. Build the
camera service separately using the native MediaPipe dependencies below.

## Build

Install a C++20 compiler, CMake 3.28 or later, Ninja, `libv4l-dev`, `unzip`,
and the EGL and GLES libraries (`libegl1` and `libgles2`), which
`libmediapipe.so` links against. Use the C headers from MediaPipe commit
`f8ef212d5c962c0e853db7e59d217056b187084b` with the native library from
MediaPipe 0.10.35. The official Linux amd64 wheel contains
`mediapipe/tasks/c/libmediapipe.so`. The wheel's SHA-256 is
`db9a579df48cffe9570cd3e93f6a5d2dd089a1103b846c60c5b5de8a21c38db0`, and the
library's is `7ba887d5e09d87440b3713816ec02897d6a849eadf2325c9aabe3707cbdac86b`.
The camera daemon links against the C library directly.
[MediaPipe 0.10.35](https://pypi.org/project/mediapipe/0.10.35/)

`scripts/fetch_mediapipe_c.sh` fetches both: the nine headers the face
landmarker's C API includes, into `include`, and the library, into `lib`. It
checks every file against `scripts/mediapipe_c.sha256`, and keeps one already
in place with its hash. CI's `linux`, `sanitizers` and `thread-sanitizer`
jobs build the daemon and its Linux units this way (#83).

Set `MODEL` to the face landmarker task that `tools/fixtures/build.sh` pins:
`face_landmarker_v2_with_blendshapes.task`, generation `1782184688504388` of
MediaPipe's test assets, whose SHA-256 is
`b261925d4aad812b47a0e8d58c1baa1223270a5d1f663d78338bc881c003879d`.
`build.sh` itself builds the macOS shim and does not run on Linux, where
`src/linux/mediapipe_c_shim.cc` gives the daemon the face model's C
functions over `libmediapipe.so`.

```sh
bash scripts/fetch_mediapipe_c.sh build-mediapipe-c
MEDIAPIPE_SOURCE="$PWD/build-mediapipe-c/include"
MEDIAPIPE_LIBRARY="$PWD/build-mediapipe-c/lib/libmediapipe.so"
```

```sh
cmake -S . -B build-linux-camera -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_INSTALL_LIBDIR=lib \
  -DFACEPALM_MEDIAPIPE_C_INCLUDE="$MEDIAPIPE_SOURCE" \
  -DFACEPALM_MEDIAPIPE_C_LIBRARY="$MEDIAPIPE_LIBRARY"
cmake --build build-linux-camera --target facepalm_daemon
cmake --install build-linux-camera --component LinuxCamera \
  --prefix "$HOME/.local/facepalm"
install -D -m 0644 "$MODEL" \
  "$HOME/.local/facepalm/share/facepalm/model/face_landmarker.task"
```

## Run

Run the daemon in a terminal to grant consent for that run. The socket
directory and the client token are private to your user. The camera opens
when an approved client requests reactions and closes after it disconnects.
Start a new MCP session after starting the daemon if an earlier connection
failed.

```sh
umask 077
runtime="${XDG_RUNTIME_DIR:-${XDG_STATE_HOME:-$HOME/.local/state}}/facepalm"
mkdir -p "$runtime"
if [ ! -e "$runtime/client-token" ]; then
  (set -C; openssl rand -hex 32 > "$runtime/client-token")
fi
app="$HOME/.local/facepalm"
"$app/bin/facepalm-daemon" --consent --socket "$runtime/service.sock" \
  --model "$app/share/facepalm/model/face_landmarker.task" \
  --client-token-file "$runtime/client-token"
```

Configure your MCP client to run the adapter with the same paths.

```sh
node "$app/share/facepalm/mcp/main.js" "$runtime/service.sock" \
  --client-token-file "$runtime/client-token"
```

The service accepts one approved client at a time. Stop the daemon with
Ctrl+C to revoke consent. If the selected camera is unplugged, reconnect it
and restart the daemon to resume capture.
