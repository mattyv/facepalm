---
name: feel
description: Tell Facepalm how you really felt at a beat this Claude Code session marked.
argument-hint: "[interpretation-id] [feeling]"
disable-model-invocation: true
---

# Tell Facepalm how you felt

If `$ARGUMENTS` gives an interpretation ID and a feeling, both from the user's
own words: the ID must be an integer, and the feeling one of the names in
that interpretation's `feelings` list, such as `agreement` or `confused`. If
either is given but invalid, ask for it without calling a tool. Call the
configured Facepalm MCP `tell_feeling` tool once with that ID and feeling,
and report its result.

Otherwise, use the most recent beat's interpretation from this conversation
(its `id`, `feeling` and `feelings`, from a `wait_for_reaction` call that
passed `beat`). If there is none, say so without calling a tool. Call
`ask_feeling` once with that `id`, `feeling` and `feelings`: it asks the
person how they felt, through a form, pre-selecting Facepalm's own read, and
sends their pick to Facepalm itself. Report what it returns. Do not also
call `tell_feeling` for the same interpretation; `ask_feeling` already took
care of it.

Facepalm refuses a feeling for an interpretation another session was given,
one already given a feeling, or one more than five minutes old.
