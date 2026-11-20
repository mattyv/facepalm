---
name: check
description: Check Facepalm reactions since this session last checked.
disable-model-invocation: true
---

Use the configured Facepalm MCP `wait_for_reaction` tool with `timeout_ms: 0`.
Show each returned reading's summary verbatim with its exact ID, and report any
missed count. If there are no readings, say so; this alone does not establish
whether the camera is active. If the tool is unavailable or returns an error,
report that the check failed. Do not start the daemon or change camera consent.
