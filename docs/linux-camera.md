# Linux camera daemon development build

Facepalm uses the existing consent, camera, analysis, and MCP service code
with a single-plane Video4Linux device on Linux. The default camera is
`/dev/video0`. Set `FACEPALM_CAMERA_DEVICE` to select another device. The
Debian desktop package contains the tray app and gesture replay. Build the
camera service separately using the native MediaPipe dependencies below.

## Build

Install a C++20 compiler, CMake 3.28 or later, Ninja, and `libv4l-dev`. Use the
C headers from MediaPipe commit `f8ef212d5c962c0e853db7e59d217056b187084b`
with the native library from MediaPipe 0.10.35. The official Linux amd64
wheel contains `mediapipe/tasks/c/libmediapipe.so`. Its SHA-256 is
`db9a579df48cffe9570cd3e93f6a5d2dd089a1103b846c60c5b5de8a21c38db0`.
The camera daemon links against the C library directly. [MediaPipe 0.10.35](https://pypi.org/project/mediapipe/0.10.35/)

Set `MEDIAPIPE_SOURCE` to the checkout, `MEDIAPIPE_LIBRARY` to the extracted
`libmediapipe.so`, and `MODEL` to the checksum-verified face landmarker task
used by `tools/fixtures/build.sh`.

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

Approve the MCP adapter once, then run the daemon in a terminal to grant
consent for that run. `approve` writes a new random token to
`~/.local/state/facepalm/clients/mcp`, private to your user, and the daemon
listens at `service.sock` in the same folder. The camera opens when an
approved client requests reactions and closes after the last one
disconnects. Start a new MCP session after starting the daemon if an
earlier connection failed.

```sh
app="$HOME/.local/facepalm"
"$app/bin/facepalm-daemon" approve mcp
"$app/bin/facepalm-daemon" --consent \
  --model "$app/share/facepalm/model/face_landmarker.task"
```

Configure your MCP client to run the adapter with no arguments; it finds the
socket and its token in that folder.

```sh
node "$app/share/facepalm/mcp/main.js"
```

Several approved clients can be connected at once, and each gets every
reaction. `facepalm-daemon revoke mcp` removes the approval. Stop the daemon
with Ctrl+C to revoke consent. If the selected camera is unplugged, reconnect it
and restart the daemon to resume capture.
