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

If you want Facepalm to access your camera, approve the bundled MCP adapter
once, then start the daemon. Set `APP` to the copied bundle's absolute path.
`approve` writes a new random token to
`~/Library/Application Support/Facepalm/clients/mcp`, private to you, and
prints its path; the daemon listens at `service.sock` in the same folder.

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
or adjust gesture confidence and expression tuning. To apply saved settings
on the next daemon start, add `--config "${XDG_CONFIG_HOME:-$HOME/.config}/facepalm/facepalm.conf"`
to the daemon command. The dialog shows the path it used. Several approved
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
