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

For Claude Desktop, add a `facepalm` entry to `claude_desktop_config.json`
(macOS: `~/Library/Application Support/Claude/claude_desktop_config.json`),
filling in `APP` as above; the adapter takes no arguments once `mcp` is
approved, so this is the whole entry:

```json
{
  "mcpServers": {
    "facepalm": {
      "command": "APP/Contents/Resources/node/bin/node",
      "args": ["APP/Contents/Resources/mcp/main.js"]
    }
  }
}
```

Restart Claude Desktop afterward. `wait_for_reaction` with a `beat` can then
show its "pick the feeling" card inline, when Claude Desktop renders it
(MCP Apps): ask Claude to call `show_feeling_card` with the `id`, `feeling`
and `feelings` a beat returned, and watch for the card asking "How were you
feeling just then?" with Facepalm's read already highlighted. Once a button
is picked, the card calls `app.updateModelContext()` with the pick, where the
host accepts it, so Claude's own context carries the pick without a further
call; whether Claude Desktop actually surfaces this needs a live check, not
exercised by any test here. A host without MCP Apps gets `show_feeling_card`'s
text instead, with the same information; `get_feeling {id}` is the fallback
everywhere, for finding out which feeling now stands when nothing else
surfaced it.

Choose **Settings** from the 🤦 menu to set the frame rate, choose the camera
(item 10 of #61, #13), enable detectors, or adjust gesture confidence and
expression tuning. The camera picker lists what `facepalm-daemon cameras`
finds right now, whether or not Facepalm is on; a choice no longer connected
stays in the picker, marked "not connected", so saving the other settings
does not quietly switch it back to the default. Turn Facepalm off and on
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
been tested with live capture. Choosing **Show Camera Preview** from the 🤦
menu opens a window of the camera while it is on, started only by hand
(#37 item 8, #108): it stops the same way it started, from the menu item, the
window's own close button, or the camera going off, and no frame reaches
anything but that window.

Command-Tab, the Dock and the app switcher reach Settings, the preview and
the calibration wizard while any one of them is open (#146): the 🤦 menu is
otherwise a menu-bar-only accessory app, invisible to all three, but switches
to a regular app the moment the first of these windows opens and back the
moment the last one closes.

## Calibration (optional)

A short guided session improves on the coarse, uncalibrated gaze of item 11
(#36, #35): it records the resting pose and, for each screen, the head pose
at its centre and four corners, then saves them to a private file the daemon
owns. Choosing **Calibrate…** from the 🤦 menu or the tray (#118) opens a
small window with a plain explanation that it is optional, then steps
through the resting pose and each screen's centre and four corners with
Next and Cancel; closing the window at any point, whether by Cancel or its
own close control, asks the daemon to cancel cleanly, so the old profile is
always what is left in place unless every step finished. `calibrate_cli`
drives the same session from a terminal, for testing and as the model
`src/calibration_flow.h`'s `CalibrationFlow` follows for the menu's own
socket client. Approve it once by hand, as any other client, only if
driving it this way rather than through the menu:

```shell
facepalm-daemon approve calibrate
calibrate_cli start 1
calibrate_cli next   # Once per prompted target.
calibrate_cli cancel # Instead of next, to give up.
```

Any client already speaking the protocol can send the same `calibrate start
[screens]|next|cancel` line directly, with its own name and token. The daemon
is wired to a real session and the camera's lifecycle (#116): a session can
only start, or keep running, while the camera is actually open, and is
cancelled the moment the camera closes or is lost, the same guarantee
`specs/Calibration.tla` makes. The profile a finished session saves feeds
the coarse gaze item 11's `context` reports (#92): a daemon run that starts
after a profile exists locates the nearest screen from its calibrated
points instead of guessing from the screen's edges alone. That profile is
loaded once, at startup, so finishing a session only changes coarse gaze
from the daemon's next run, not the one the session ran against. The menu
approves its own "calibrate" client the first time **Calibrate…** is
chosen, the same way **Turn On Facepalm…** approves its own client, rather
than when the menu itself starts.
