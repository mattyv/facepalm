---
name: feel
description: Tell Facepalm how you really felt at a beat this Claude Code session marked.
argument-hint: "[interpretation-id] [feeling]"
disable-model-invocation: true
---

# Tell Facepalm how you felt

Use only the interpretation ID and the feeling the user gave in `$ARGUMENTS`.
The ID must be an integer, and the feeling one of the names in that
interpretation's `feelings` list, such as `agreement` or `confused`. If either
is missing or invalid, ask for it without calling a tool.

Call the configured Facepalm MCP `tell_feeling` tool once with that ID and
feeling. Report its result. Facepalm refuses a feeling for an interpretation
another session was given, one already given a feeling, or one more than five
minutes old.
