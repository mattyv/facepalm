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
menu opens a window of the camera, started only by hand (#37 item 8, #108)
and turning the camera on by itself if nothing else has it on already
(#176, `src/camera_demand.h`): it stops the same way it started, from the
menu item, the window's own close button, or the camera going off, and no
frame reaches anything but that window. Beside the frame, the window shows
what Facepalm is reading as you watch yourself (#134): each gesture or
expression it reports, held for two seconds so it can be read, and the
latest beat's feeling (such as "Last read: confused (question)"), which
stays until the next one. Both are a diagnostic only, text written to a
private file beside the frame under the same rules, read only by the menu
and only while the preview streams.

Command-Tab, the Dock and the app switcher reach Settings, the preview and
the calibration wizard while any one of them is open (#146): the 🤦 menu is
otherwise a menu-bar-only accessory app, invisible to all three, but switches
to a regular app the moment the first of these windows opens and back the
moment the last one closes.

### Menu items by state (#176)

`src/menu_state.h`'s `DecideMenuState` is the one table both the macOS
menu (`tools/menu/main.mm`) and the Linux tray (`tools/menu/main_linux.cc`)
read for the always-disabled camera-status line and the **Show Camera
Preview** and **Calibrate…** items, so the two can never disagree about
when an item is enabled or what it says. Disabled, titled "Turn on
Facepalm first", was chosen over leaving the items enabled with an offer
to turn Facepalm on from inside them: **Turn On Facepalm…** already sits
one line above either item, so a second way to reach it would be a second
place for the same choice to drift out of sync, for no benefit over
simply pointing at the one that already works (`docs/decisions.md`).

| Facepalm | Camera | Preview window | Camera-status line | Show Camera Preview | Calibrate… |
|---|---|---|---|---|---|
| Off | (not running) | (not running) | "Facepalm off" | Disabled, "Turn on Facepalm first" | Disabled, "Turn on Facepalm first" |
| On | Idle | Closed | "Camera off" | Enabled, "Show Camera Preview" | Enabled, "Calibrate…" |
| On | Idle | Open | "Camera off" | Enabled, "Hide Camera Preview" | Enabled, "Calibrate…" |
| On | Running | Closed | "Camera on" | Enabled, "Show Camera Preview" | Enabled, "Calibrate…" |
| On | Running | Open | "Camera on" | Enabled, "Hide Camera Preview" | Enabled, "Calibrate…" |

"Camera" and "Preview window" are independent: opening the preview is
itself what can move "Idle" to "Running" (#176), so the second and third
rows, and the fourth and fifth, are both reachable, not a contradiction --
a camera already running for an approved client's sake shows exactly the
same "Running" either way. Calibrate… is never disabled by the preview
being open: it speaks to the daemon over its own, separate connection.
`test/menu_state_test.cc` covers every combination (service on, camera in
use, preview open) a solitary test can reach; the table above is `service
on` crossed with the two that matter to what is shown (camera in use
decides the status line and nothing else; disabled items ignore both).

## Calibration (optional)

Item 11's coarse gaze (#36, #35) reads head pose composed with eye-in-head
signal (`src/eye_gaze.h`'s `ComposeGaze`, #137), not head pose alone: eye
movement that keeps the head still still shows up as a look elsewhere,
and a person who turns their head one way while looking the other with
their eyes reads correctly rather than backwards. A short guided session
improves further on this, still coarse but now calibrated, reading: it
records the resting head pose and eye level, a gains step (fixate a dot
and slowly turn the head left then right, keeping your eyes on it --
`eye_gaze.*` settings' defaults, unproven on a real recording, see the
tuning note below), and, for each screen, the composed gaze at its centre
and four corners, then saves them to a private file the daemon owns. A
step whose data is too weak to use -- the gains step with too little head
travel, or a corner too close to its own screen's centre -- is refused
with a plain reason and retried, rather than accepted and silently
useless (#136). Choosing **Calibrate…** from the 🤦 menu or the tray
(#118) opens a small window with a plain explanation that it is optional,
then steps through the resting pose, the gains step, and each screen's
centre and four corners with Next and Cancel, moving itself onto the
screen a step names and naming that screen (its position and display
name) in the prompt, for every screen the computer has (#135); closing
the window at any point, whether by Cancel or its own close control, asks
the daemon to cancel cleanly, so the old profile is always what is left
in place unless every step finished, and the wizard ends with a plain
result, "Calibrated N screens." `calibrate_cli` drives the same session
from a terminal, for testing and as the model `src/calibration_flow.h`'s
`CalibrationFlow` follows for the menu's own socket client. Approve it
once by hand, as any other client, only if driving it this way rather
than through the menu:

```shell
facepalm-daemon approve calibrate
calibrate_cli start 1
calibrate_cli next   # Once per prompted target (the resting pose, the
                      # gains step, then each screen's five points).
calibrate_cli cancel # Instead of next, to give up.
```

Any client already speaking the protocol can send the same `calibrate start
[screens]|next|cancel` line directly, with its own name and token. The daemon
is wired to a real session and the camera's lifecycle (#116): a session can
only start, or keep running, while the camera is actually open, and is
cancelled the moment the camera closes or is lost, the same guarantee
`specs/Calibration.tla` makes. The profile a finished session saves feeds
the coarse gaze item 11's `context` reports (#92): a daemon run with a
profile locates the nearest screen from its calibrated points instead of
guessing from the screen's edges alone, and its own gains take over from
`eye_gaze.horizontal_gain`/`eye_gaze.vertical_gain`'s configured default
for every reading. Finishing a session applies its profile to that
reading at once (`ScreenContextReporter::SetProfile`, called from
`CalibrationSession`'s own `on_finished` callback): no restart needed
(#136). A profile a version before #137 saved has no gains or resting eye
level, so it fails to load (`calibration_store.cc`'s own version check)
and the daemon reads coarse gaze as uncalibrated until it is redone. The
menu approves its own "calibrate" client the first time **Calibrate…** is
chosen, the same way **Turn On Facepalm…** approves its own client, rather
than when the menu itself starts.

**Tuning note (#137):** every `eye_gaze.*` and `calibration.min_*` default
in `src/config.h` was given from memory, the way #139's eyes line's own
field order was, not measured on a real recording: `eye_gaze.horizontal_gain`
and `eye_gaze.vertical_gain` (how many degrees of gaze one unit of eye-look
score is worth), `eye_gaze.blink_threshold`, `eye_gaze.median_ms`,
`eye_gaze.resting_ms`, `eye_gaze.gap_ms`, `eye_gaze.head_only_yaw_degrees`,
and `calibration.min_head_travel_degrees`, `calibration.min_axis_degrees`
and `calibration.min_spread_degrees`. Once #139's fixtures exist, replay
them through `src/eye_gaze.h`'s `EyeSignalFrom` and `EstimateGains` to see
whether these hold up, the way #34's and #46's own defaults were tuned on
a real recording.
