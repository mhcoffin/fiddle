# Fiddle MCP control

Fiddle ships a local Model Context Protocol (MCP) adapter so AI agents can
inspect and control the running audio application without driving its WebView.

## Architecture

The integration has two deliberately separate pieces:

1. `FiddleServer` owns an authenticated TCP bridge bound to `127.0.0.1` on an
   operating-system-assigned port. It publishes the current endpoint and a
   random per-launch token to
   `~/Library/Application Support/Fiddle/agent-control.json`. The descriptor is
   readable and writable only by the current user and is removed on a clean
   shutdown.
2. `fiddle-mcp.mjs` is a local stdio MCP server built with the official
   TypeScript SDK. An MCP client launches it; it reads the descriptor and sends
   one authenticated request to the running Fiddle application for each tool
   call.

This keeps MCP version and transport handling out of the real-time application.
All live reads and writes execute on JUCE's message thread. Mutations go through
the same command services and Undo history as the UI; the bridge never touches
the audio thread or mutable DSP structures directly.

## Tools

| Tool | Effect |
| --- | --- |
| `fiddle_get_status` | Read application, transport, project, dirty, and history status. |
| `fiddle_get_mixer` | Read layers, group buses, master state, and stable layer IDs. |
| `fiddle_set_layer_gain` | Set one layer's gain from -120 to +6 dB. |
| `fiddle_set_layer_mute` | Mute or unmute one layer. |
| `fiddle_set_layer_solo` | Solo or unsolo one layer. |
| `fiddle_undo` | Undo the latest Fiddle edit. |
| `fiddle_redo` | Redo the next Fiddle edit. |
| `fiddle_inspect_library_sources` | Inspect expression maps in a directory and report their player/preset metadata, plus matching installed plug-ins. |
| `fiddle_create_guided_library` | Create a persistent library whose patches are ready for the user to finish in the guided setup UI. |
| `fiddle_get_library_setup` | Read setup progress and the remaining patches for one library. |
| `fiddle_open_guided_library` | Open Library Manager on a library and start or resume its guided player setup. |

The read tools are marked read-only. The setters are non-destructive and
idempotent. Undo, Redo, and library creation are non-idempotent. Opening the
guided UI is idempotent. All tools are closed-world: they only interact with
the local Fiddle process and paths supplied by the user.

## Guided library setup

Some sample-player presets cannot be selected reliably by an agent because the
choice lives inside the plug-in's own editor. The guided workflow automates the
durable and verifiable parts while leaving that choice to the user:

1. Call `fiddle_inspect_library_sources` with the user's expression-map
   directory. For a large multi-product collection, pass `map_query` (for
   example, `BBC Symphony Orchestra Discovery`) to filter by path. Each
   discovered map reports its stable map ID and any declared player/preset
   names. Fiddle asks for a filter instead of synchronously parsing more than
   2,000 files from one request.
2. Match those maps to the intended instruments and call
   `fiddle_create_guided_library`. Each patch supplies an expression map, an
   installed instrument plug-in UID, and the preset name the user should select.
3. Call `fiddle_open_guided_library` with the returned library ID. Fiddle opens
   Library Manager, loads the first unfinished player, and displays the expected
   preset.
4. The user selects that preset in the plug-in editor and presses **Capture &
   continue**. Fiddle saves the captured plug-in state immediately, closes that
   editor, and advances to the next patch.
5. If setup is interrupted, opening the library again resumes at the first
   unfinished patch. Agents can query the same progress with
   `fiddle_get_library_setup`.

The guided library is stored as soon as it is created; it is intentionally
allowed to be incomplete. A patch becomes complete only after the user changes
the player state and explicitly captures it, avoiding false confirmation of a
default or stale player state.

## Configure a client

Build Fiddle first, then point a local MCP client at the bundled adapter. For a
development Release build, the Codex project configuration is:

```toml
[mcp_servers.fiddle]
command = "node"
args = ["/absolute/path/to/fiddle/build/release/FiddleServer_artefacts/Release/FiddleServer.app/Contents/Resources/mcp/fiddle-mcp.mjs"]
default_tools_approval_mode = "writes"
```

For an installed application, replace the argument with the corresponding
path inside the installed `FiddleServer.app`. Restart the MCP client after
changing its configuration. Fiddle itself must be running and finished
starting before a tool call can succeed.

The adapter requires Node.js 20 or later. For isolated tests or nonstandard app
data locations, set `FIDDLE_AGENT_CONTROL_FILE` to an alternate descriptor
path.

## Safety boundary

- The native bridge accepts only loopback TCP connections.
- Every request must include the random token from the user-only descriptor.
- Requests and responses are limited to 1 MiB and time out instead of blocking
  indefinitely.
- Agent mutations are serialized on the message thread and enter Fiddle Undo.
- The adapter instructs agents not to change controls during Dorico playback
  unless the user explicitly asks.
- General project saving, layer creation/removal, arbitrary library editing,
  and audio device changes are not exposed. Guided library creation is narrowly
  scoped to stopped transport and validated installed plug-ins, expression
  maps, instrument IDs, and patch metadata.

## Build and test

Install the adapter dependencies once:

```sh
cd mcp
pnpm install
```

The normal CMake build type-checks and bundles the adapter. Its focused checks
are:

```sh
cmake --build build/release --target AgentControlServerTest FiddleMcpBuild
ctest --test-dir build/release -R 'agent_control_server|fiddle_mcp_adapter' --output-on-failure
```
