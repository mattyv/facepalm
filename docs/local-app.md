# Local macOS app staging

The staging scripts assemble an unsigned, local development bundle from
already-built components. They do not download, install, launch, start a
service, or grant camera access. The bundle contains a menu bar app, the
foreground daemon, and the MCP adapter.

Provide an already-built MediaPipe shim, its face model, and a self-contained
Node 22 or newer distribution containing `bin/node` and `LICENSE`. Set
`SHIM`, `MODEL`, and `NODE_DIRECTORY` to those local paths and run:

```shell
cmake -S . -B build-app -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DFACEPALM_MEDIAPIPE_DYLIB="$SHIM"
cmake --build build-app --target facepalm_menu facepalm_daemon
npm ci --prefix mcp
bash scripts/build_app.sh build-app/facepalm-menu build-app/facepalm-daemon \
  "$SHIM" "$MODEL" "$NODE_DIRECTORY" /tmp/Facepalm.app
```

The builder checks the version, adapter imports, and native library paths;
it rejects an existing output. Homebrew's Node may depend on libraries outside
the app, so a standalone Node distribution is needed. The builder includes the
project notices and Node's license. The supplied model and shim are local
inputs; this process does not establish permission to redistribute them.

To copy the staged bundle to an existing destination directory:

```shell
bash scripts/install_app.sh /tmp/Facepalm.app "$HOME/Applications"
```

The installer refuses to replace an existing `Facepalm.app`; remove or move an
old installation yourself if appropriate. It does not start Facepalm. This is
not a Developer ID-signed or notarized distribution, and the camera permission
flow for the eventual app host has not been established by this staging step.

## Run the development bundle

If you want Facepalm to access your camera, create a private runtime directory
and an approval token yourself. Keep the token file private. Set `APP` to the
copied bundle's absolute path.

```shell
runtime=$(mktemp -d /tmp/facepalm.XXXXXX)
(umask 077 && openssl rand -hex 32 >"$runtime/client-token")
"$APP/Contents/MacOS/facepalm-daemon" --consent \
  --socket "$runtime/service.sock" \
  --model "$APP/Contents/Resources/model/face_landmarker.task" \
  --client-token-file "$runtime/client-token"
```

Configure the MCP host to run the bundled Node with these arguments, using
the same absolute runtime path:

```text
Contents/Resources/node/bin/node
Contents/Resources/mcp/main.js RUNTIME/service.sock --client-token-file RUNTIME/client-token
```

Choose **Settings** from the 🤦 menu to set the frame rate, enable detectors,
or adjust gesture confidence and expression tuning. To apply saved settings
on the next daemon start, add `--config "${XDG_CONFIG_HOME:-$HOME/.config}/facepalm/facepalm.conf"`
to the daemon command. The dialog shows the path it used. Use one approved MCP
connection at a time. It receives interpreted gesture and expression events.
For Claude Code, see the [bundled plugin instructions](/integrations/claude-code/README.md).
To save ratings locally, add `--ratings-file PATH` with a private file path.
The owner-only JSONL file stores the summary, reading ID, timestamps and
score, up to 10 MiB. The daemon rejects ratings when the option is absent,
the file is full, or a write fails. A failed write or sync disables further
writes for that run and may leave an unacknowledged record. Saved ratings
do not yet change detection thresholds automatically. Stop the foreground
daemon to revoke consent for that run.

The local bundle verification covers startup refusal without consent, rejection
of an unapproved socket client, and clean shutdown. The new bundle has not
been tested with live capture. Live preview is pending.

## Calibration (optional)

A short guided session improves on the coarse, uncalibrated gaze of item 11
(#36, #35): it records the resting pose and, for each screen, the head pose
at its centre and four corners, then saves them to a private file the daemon
owns. `calibrate_cli` drives one against a running daemon, for testing and as
a model for the eventual menu entry (item 8):

```shell
calibrate_cli "$runtime/service.sock" start 1
calibrate_cli "$runtime/service.sock" next   # Once per prompted target.
calibrate_cli "$runtime/service.sock" cancel # Instead of next, to give up.
```

Any client already speaking the protocol can send the same `calibrate start
[screens]|next|cancel` line directly. The daemon itself is not yet wired to a
calibration session or the camera's lifecycle (that is item 7's follow-up, so
`calibrate_cli` has nothing to talk to on a running daemon until it lands);
until then, every `calibrate` request gets `{"error":"calibration is not
available"}`.
