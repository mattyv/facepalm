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
The same is true of the Screen Recording permission `screen_context`'s window
titles need: with `screen_context.window_titles_enabled` on, a window title
reads as absent until the eventual app host has it.

## Run the development bundle

Open the copied `Facepalm.app` and choose **Turn On Facepalm…** from the 🤦
menu. It asks for your consent, approves the bundled MCP adapter, and starts
the daemon as a launchd LaunchAgent,
`~/Library/LaunchAgents/com.mattyv.facepalm.daemon.plist`, which runs it
again if it fails and at each login until you choose **Turn Off Facepalm**.
Turning off stops the daemon, which closes the camera, and removes the
agent. The daemon's errors go to `~/Library/Logs/facepalm-daemon.log`. The 🤦
menu's disabled **Camera** line reads **Camera on** within a couple of
seconds of a client subscribing, and **Camera off** once the last one
leaves (#61 item 8); it rereads the daemon's status file,
`~/Library/Application Support/Facepalm/status`, on its own timer, so it
shows this even while the dropdown was closed when the camera started.
Running the daemon by hand with no `--status` flag skips writing it, and
the indicator then reads as off throughout.

Approving the adapter writes a new random token to
`~/Library/Application Support/Facepalm/clients/mcp`, private to you; the
daemon listens at `service.sock` in the same folder. To run the daemon by
hand instead, in a terminal, set `APP` to the copied bundle's absolute path:

```shell
"$APP/Contents/MacOS/facepalm-daemon" approve mcp
"$APP/Contents/MacOS/facepalm-daemon" --consent \
  --model "$APP/Contents/Resources/model/face_landmarker.task"
```

Configure the MCP host to run the bundled Node with the adapter and no
arguments; it finds the socket and its token in that folder:

```text
Contents/Resources/node/bin/node Contents/Resources/mcp/main.js
```

`facepalm-daemon revoke mcp` removes the approval; the next connection is
refused. Another client, such as AI Pair, is approved under its own name.

Choose **Settings** from the 🤦 menu to set the frame rate, enable detectors,
or adjust gesture confidence and expression tuning. Turn Facepalm off and on
again to apply them; the menu passes the settings file to the daemon when
there is one. By hand, add
`--config "${XDG_CONFIG_HOME:-$HOME/.config}/facepalm/facepalm.conf"` to the
daemon command. The dialog shows the path it used. Several approved
MCP connections, such as Claude Desktop's and Claude Code's, can be open at
once; each receives every interpreted gesture and expression event.
For Claude Code, see the [bundled plugin instructions](/integrations/claude-code/README.md).
The daemon keeps the feelings you pick in
`${XDG_CONFIG_HOME:-$HOME/.config}/facepalm/feelings`, or at the path given with
`--feelings-file PATH`: a private text file of counts, one line for each
reaction, kind of beat and feeling, with no images, times or summaries. It
tunes later reads from them. A client clears them with `reset feelings`; so
does deleting the file while the daemon is stopped. Stop the foreground
daemon to revoke consent for that run.

The local bundle verification covers startup refusal without consent, rejection
of an unapproved socket client, and clean shutdown. The new bundle has not
been tested with live capture. Live preview is pending.
