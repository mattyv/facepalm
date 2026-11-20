---
name: rate
description: Rate a Facepalm reading that this Claude Code session received.
argument-hint: "[reading-id] [1–5]"
disable-model-invocation: true
---

Use only the reading ID and score the user gave in `$ARGUMENTS`. Both must
be integers and the score must be between 1 and 5. If either is missing or
invalid, ask for it without calling a tool.

Call the configured Facepalm MCP `rate_reaction` tool once with that ID and
score. Report its result. A different session, an expired reading, or a
daemon without local ratings enabled can reject the rating.
