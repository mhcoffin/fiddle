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

Read-only:

| Tool | Returns |
| --- | --- |
| `fiddle_get_status` | Application, transport, project, dirty and history status, including the library history, unsaved Library Manager edits and a running plug-in scan. |
| `fiddle_get_mixer` | Layers (with plug-in status), group buses, master state and stable layer IDs; filter by words, chair or layer. |
| `fiddle_search_dorico_instruments` | Canonical Dorico instrument IDs by name, with family, suggested chair role and a kit's instruments. |
| `fiddle_list_plugins` | Known plug-ins with UIDs, instrument/effect compatibility and whether each loaded; `scanning` while a scan runs. |
| `fiddle_list_maps` | Fiddle's expression and percussion maps and how many patches and layers use each; percussion maps can be filtered by an instrument they cover. |
| `fiddle_inspect_library_sources` | Expression- or percussion-map metadata in `.doricolib` files (a given folder, or the folders Dorico loads libraries from), whether Fiddle has each map, and matching instrument plug-ins. |
| `fiddle_list_libraries` | Libraries with patch counts, patches awaiting a preset capture, and layer counts. |
| `fiddle_get_library` | One library's patches in full. |
| `fiddle_find_patches` | Patches across every library, by words, Dorico instrument (and its variants) or player. |
| `fiddle_get_library_setup` | Guided-setup progress for one library. |
| `fiddle_get_chairs` | Chairs (including empty ones) with instrument, role, MIDI port and channel, kit and layers; kits; and whether Dorico's playback template is out of date. Filter by words, chair, or chairs without layers. |
| `fiddle_list_score_test_runs` | Recent score test runs with pass/fail and note counts. |
| `fiddle_get_score_test_run` | A score test run explained by its ID: failed, missing and extra notes, part bindings, bridge state, and optionally one note's checks and the MIDI around it. |

Edits:

| Tool | Effect | History |
| --- | --- | --- |
| `fiddle_set_layer_gain`, `_mute`, `_solo` | One layer's gain (-120 to +6 dB), mute or solo. | project |
| `fiddle_scan_plugins` | Start an incremental or full VST3 scan in the background. | none |
| `fiddle_save_library` | Create a library or edit one: rename, add, change or remove patches; import the maps they need. | library |
| `fiddle_delete_library` | Delete a library no layer uses. | library |
| `fiddle_create_guided_library` | Create a library whose patches all wait for the user to capture a preset. | library |
| `fiddle_open_guided_library` | Open Library Manager at a library's first unfinished preset. | none |
| `fiddle_create_chairs` | Add chairs, or instruments to an existing kit, in one step. | project |
| `fiddle_update_chair` | Rename a chair or change its role. | project |
| `fiddle_delete_chair` | Delete a chair and its layers. | project |
| `fiddle_create_kit`, `_rename_kit`, `_delete_kit` | Percussion kits ([percussion kits](percussion-kits.md)). | project |
| `fiddle_add_layer`, `fiddle_remove_layer` | Add a library patch to a chair as a layer, or remove one (optionally across a kit). | project |
| `fiddle_install_playback_template` | Write the Dorico playback template for the current chairs. | none |
| `fiddle_undo`, `fiddle_redo` | Step the project history (default) or the library history. | either |

Conventions:

- MIDI channels are numbered 1-16 in both `fiddle_get_chairs` and
  `fiddle_get_mixer` (a layer with no input has port -1, channel 0).
- Searches (`query`) match when every word starts a word in the text, or,
  for words of four or more letters, appears anywhere once spaces and
  punctuation are ignored: "hi" finds "Hi-hat" but not "China", and "hihat"
  finds "Hi-Hat".
- A new layer's plug-in loads in the background; `fiddle_get_mixer` with its
  layer ID shows when it is `loaded` or reports an error.
- Undo and Redo answer with the step they moved and both histories, not the
  whole mixer. Chair edits return only the chairs they touched.

Tools are annotated for MCP clients: reads are read-only; deletions, layer
removal and template installation are destructive; setters are idempotent.
The score test tools read run records from
`~/Library/Application Support/Fiddle/ScoreTests/runs` directly
(`FIDDLE_SCORE_TEST_RUNS_DIR` overrides it), so they work when Fiddle is not
running; see `tests/score/README.md`. All tools are closed-world: they only
interact with the local Fiddle process and paths supplied by the user.

The C++ side is `AgentControl.cpp` (a table from wire method to handler) with
the request logic that needs no running app in `AgentSetupRequests.{h,cpp}`
(`AgentSetupRequestsTest`). The adapter is `mcp/src/index.ts` and
`mcp/src/setup-tools.ts`.

## Setting up a library with an agent

An agent can take a user from "I just installed a sample library" to Dorico
playing it ([library, chair and layer architecture](library-chair-layer-architecture.md)):

1. **Find the player.** `fiddle_list_plugins`; if the plug-in is missing,
   `fiddle_scan_plugins` and poll `fiddle_list_plugins` until `scanning` is
   false.
2. **Find what exists.** `fiddle_find_patches` for patches Fiddle already has for
   the instrument; `fiddle_list_maps` for maps Fiddle already has;
   `fiddle_inspect_library_sources` (no folder needed for maps installed in
   Dorico) for the library's own `.doricolib` maps.
3. **Build the library.** `fiddle_save_library` with one patch per sound:
   name, Dorico instrument (`fiddle_search_dorico_instruments`), player
   plug-in, and optionally maps and an expected preset. Patches may use
   different players (one plug-in per instrument, as in SWAM) and need no map.
   Present the plan to the user first.
4. **Capture presets.** Where the preset can only be chosen in the player's
   editor, give `expected_preset_name`, then `fiddle_open_guided_library`. The
   user loads each preset and presses **Capture & continue**; see below.
5. **Create chairs.** `fiddle_get_chairs` for what exists, then
   `fiddle_create_chairs` (for example the ensemble in [Setup goal](Setup%20goal.md))
   and `fiddle_create_kit` for percussion kits.
6. **Add layers.** `fiddle_add_layer` for each chair and patch.
7. **Install the template.** If `fiddle_get_chairs` reports
   `templateStatus.dirty`, ask the user to quit Dorico, then
   `fiddle_install_playback_template`. The user restarts Dorico and applies
   the Fiddle template in open projects.

## Guided library setup

Some sample-player presets cannot be selected reliably by an agent because the
choice lives inside the plug-in's own editor. The guided workflow automates the
durable and verifiable parts while leaving that choice to the user:

1. Call `fiddle_inspect_library_sources`, with the user's expression-map
   directory or none for Dorico's library folders. For a large multi-product
   collection, pass `map_query` (for example, `BBC Symphony Orchestra
   Discovery`) to filter by path. Each discovered map reports its stable map ID
   and any declared player/preset names. Fiddle asks for a filter instead of
   synchronously parsing more than 2,000 files from one request.
2. Match those maps to the intended instruments and call
   `fiddle_create_guided_library` (one player for every patch, a map for each)
   or `fiddle_save_library` with `expected_preset_name` on each patch.
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
default or stale player state. Changing a patch's player or expected preset
with `fiddle_save_library` makes it wait for a capture again. A layer added
from a patch that is still waiting starts from whatever state the patch has:
the player's default, or the state captured for its previous preset;
`fiddle_add_layer` says which.

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

Agents can do most of what the user can do to set Fiddle up, because setup is
where an agent helps most. The boundary is about what an agent can break and
whether the user can take it back.

- The native bridge accepts only loopback TCP connections, and every request
  must include the random token from the user-only descriptor.
- Requests and responses are limited to 1 MiB and time out after 10 seconds
  instead of blocking. Long work (a plug-in scan) starts in the background and
  is polled.
- Requests run one at a time on the message thread and use the same command
  paths, validation and Undo histories as the UI: chair, kit, layer and mixer
  edits on the project history; library and map edits on the library history.
  Every agent edit except a plug-in scan and a template install can be undone
  with `fiddle_undo`.
- Setup edits (libraries, chairs, kits, layers, the template), and Undo and
  Redo in either history, are refused while Dorico is playing. The adapter tells agents not to change anything
  else during playback unless the user asks.
- Library edits are refused while Library Manager has unsaved edits, because
  saving that draft would overwrite them. After an agent edit, an open,
  unedited Library Manager editor reloads.
- Deleting a library, or removing a patch, is refused while a layer uses it.
- Deletions and template installation are annotated destructive, so MCP
  clients ask before running them. The tool descriptions tell agents to
  present a plan before creating libraries or several chairs, and to have the
  user quit Dorico before installing the template.
- Installing the playback template writes Dorico's user library and isn't
  undoable; it is the same file the Chairs window's Install button writes, and
  installing again after an undo puts Dorico back in step.
- Not exposed: saving or checking out project versions, deleting maps, audio
  device and printing settings, and choosing a preset inside a plug-in's
  editor (guided capture leaves that to the user).

## Build and test

Install the adapter dependencies once:

```sh
cd mcp
pnpm install
```

The normal CMake build type-checks and bundles the adapter. Its focused checks
are:

```sh
cmake --build build/release --target AgentControlServerTest AgentSetupRequestsTest FiddleMcpBuild
ctest --test-dir build/release -R 'agent_control_server|agent_setup_requests|fiddle_mcp_adapter' --output-on-failure
```
