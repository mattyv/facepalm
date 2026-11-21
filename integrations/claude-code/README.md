# Facepalm commands for Claude Code

This local plugin provides `/facepalm:check`, which asks the configured
Facepalm MCP server for reactions since this MCP session last checked. It
shows Facepalm's summaries and reading IDs. An empty result does not prove the
camera is active. A failed or unavailable tool is reported as a failed check.

Start Claude Code with this plugin directory, using the absolute path to your
Facepalm checkout:

```shell
claude --plugin-dir /path/to/facepalm/integrations/claude-code
```

The plugin does not configure MCP, start Facepalm, or grant camera consent.
Configure the Facepalm stdio MCP server separately for that Claude Code
session, using the daemon's socket and private token file. The daemon currently
allows one approved connection: another active listener can prevent this
session from connecting. The token file path may be passed as a process
argument; never paste the token itself into chat or a command.

For example, after choosing the socket and token-file paths used by the daemon,
register the installed MCP adapter in the current project's local Claude Code
configuration (replace the placeholders):

```shell
SOCKET_PATH=/path/to/facepalm.sock
TOKEN_FILE=/path/to/private-token
APP="$HOME/Applications/Facepalm.app/Contents/Resources"
claude mcp add --scope local facepalm -- \
  "$APP/node/bin/node" "$APP/mcp/main.js" "$SOCKET_PATH" \
  --client-token-file "$TOKEN_FILE"
```

Use `/facepalm:settings` to open Settings when the macOS menu build is
installed. Use `/facepalm:feel INTERPRETATION_ID FEELING` to tell Facepalm how
you really felt at a beat this session marked; it tunes later reads from your
picks. The plugin has no daemon
status or stop command. These slash
commands are specific to Claude Code.

## Optional action hooks

After configuring the MCP server above, start Claude through the local wrapper
instead of the direct command:

```shell
FACEPALM_NODE="$APP/node/bin/node" bash "$APP/claude-code/facepalm-claude.sh"
```

The wrapper creates a private temporary local socket directory and removes it
when Claude exits. Set `FACEPALM_NODE` to the installed bundle's Node binary
if it is not on `PATH`. The optional broker runs inside the MCP server process
and shares its one daemon connection and cursor; the hook does not connect to
the daemon separately. If MCP is unavailable, busy, or has not started yet,
the hook adds no context and does not block the tool action. The MCP check
remains available as a fallback.

Hooks send only a session identifier, tool-use identifier, event and a small
allowlisted tool category to the local broker, never tool arguments or results.
A returned reaction may be labeled as published during an action based on its
publication timestamp; this is temporal proximity, not evidence of intent or
causation. Parallel actions may overlap. Publication time is not capture time.
The broker does not persist action markers. If a hook disconnects before
acknowledging a delivery, it can be replayed; a crash after context output but
before acknowledgement can therefore repeat it. Abrupt process termination may
leave the wrapper's temporary directory behind.
