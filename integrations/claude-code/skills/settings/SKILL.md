---
name: settings
description: Open the installed Facepalm settings window.
disable-model-invocation: true
---

# Open Facepalm Settings

Call the `open_facepalm_settings` MCP tool (facepalm's own adapter, not a
shell command: it finds the installed menu app or tray on either macOS or
Linux and checks it before launching). Report its result as given. Opening
settings does not start the camera, change consent, or apply settings to
an already running daemon, and the tool does not confirm the window became
visible.
