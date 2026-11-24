# Linux camera daemon development build

Facepalm uses the existing consent, camera, analysis and MCP service code
with a single-plane Video4Linux device on Linux. Configure
`camera.device_id` in `~/.config/facepalm/facepalm.conf`: blank selects the
first usable color camera discovered under `/dev/video*`, including USB
cameras. An explicit choice continues to match `v4l2_capability.bus_info`.
`FACEPALM_CAMERA_DEVICE`, which named a device path directly, is retired.

The Ubuntu camera package includes the service; see
[Ubuntu installation](ubuntu-release.md). The Debian desktop package contains
the tray and gesture replay; build its camera service separately below.

## Selection and diagnostics

Selection checks the opened node's capture and I/O capabilities, rather than
assuming every node belonging to one physical device provides image data.
Metadata nodes, infrared-name hints and unrecognized native image formats are
rejected. Emulated RGB alone does not establish that a sensor captures color.
The accepted native formats are RGB24, BGR24, MJPEG, JPEG, YUYV, UYVY, YVYU,
NV12, YUV420 and YVU420. This conservative list may reject other valid color
formats; the diagnostic reports that limitation. Names and formats cannot
prove a sensor's optical spectrum, so hardware verification is still needed.

When supported, `VIDIOC_TRY_FMT` checks RGB24 conversion before selection.
Drivers that do not implement that optional ioctl proceed to the existing
capture format negotiation. Each probe logs its node, capabilities, advertised
formats, rejection reason and relevant errno; the selected node is logged too.
The [kernel metadata interface][metadata] explains why one physical camera
can expose separate metadata nodes.

## Recovery and target-machine checks

Unavailable cameras retry while consent and client or preview demand remain.
The delay grows from 1 to 2, 4, 8, 16 and at most 30 seconds. The camera and
previous model finish retiring before another open is admitted. Turning off
or ending demand cancels recovery; the service does not capture while idle.
A bounded calibration attempt can finish with a reason and retry guidance;
it does not become an indefinite camera subscription.

Before upgrading, turn Facepalm off and quit the old tray. After installing,
launch the new tray and turn it on. Turn On regenerates the per-user service
unit with the new restart budget; restarting an old unit alone does not
rewrite its settings.

For the systemd test, run the following, then choose **Show Camera Preview**
so the camera actually has demand:

```sh
systemctl --user restart facepalm
systemctl --user show facepalm -p MainPID -p ActiveState -p SubState -p NRestarts
facepalm-daemon cameras
journalctl --user -u facepalm.service --since '5 minutes ago' --no-pager
```

Repeat using tray **Turn Off Facepalm**, **Turn On Facepalm…**, then
**Show Camera Preview** and **Calibrate…**. Check that the visible-light
image is selected, unavailable devices recover without process restart
bursts, and closing a window or turning off releases camera demand. Record
these checks on #195. Automated mock and lifecycle tests do not establish
that a particular camera driver or physical machine works.

The legacy status file's first `on`/`off` line still describes camera use,
not daemon liveness. A running service with no camera demand is correctly
idle and may have no video file descriptor. The tray reports service health
and camera activity separately using the versioned health record.

[metadata]: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-meta.html

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

Choose **Turn On Facepalm…** from the tray to give your consent and start the
daemon as a systemd user unit, `~/.config/systemd/user/facepalm.service`,
which runs it again if it fails and at each login until you choose **Turn
Off Facepalm**. The tray looks for the daemon and its model in its own
install, then in `~/.local/facepalm`, where the commands above put them.
Turning on also approves the MCP adapter.

To run it by hand instead, approve the MCP adapter once, then run the daemon
in a terminal to grant consent for that run. `approve` writes a new random
token to `~/.local/state/facepalm/clients/mcp`, private to your user, and the
daemon listens at `service.sock` in the same folder. The camera opens when an
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
with Ctrl+C to revoke consent. If the selected camera is unplugged, the daemon
notices over `/dev`'s own inotify events and reopens it once it is plugged
back in, with no restart needed (#111).
