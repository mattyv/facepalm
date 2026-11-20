---
name: settings
description: Open the installed Facepalm settings window on macOS.
disable-model-invocation: true
---

Use `~/Applications/Facepalm.app` or the app path the user provides. Check
that `Contents/Info.plist` names `facepalm-menu` as `CFBundleExecutable`.
If it does, call `open -a "$app" --args --settings` with that path. If it
does not, report that this build has no settings window.
Report an error if the app cannot be opened. Opening settings does not start
the camera or apply settings to an already running daemon. Do not claim the
window is visible unless that was verified.
