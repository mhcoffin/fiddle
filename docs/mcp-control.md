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
| `fiddle_get_status` | Application, transport, project, dirty and history status, including the library history, unsaved Library Manager edits, a running plug-in scan, whether Dorico is connected, what the user was told when the Dorico project connected (`projectRestore`), and the warning Fiddle shows when Dorico tried to connect a second Fiddle (a second open project, or a template with too few chairs). |
| `fiddle_get_mixer` | Layers (with plug-in status) and stable layer IDs, plus group buses and master state; filtering by words, chair or layer returns only the layers. |
| `fiddle_search_dorico_instruments` | Canonical Dorico instrument IDs by name, with family, suggested chair role and a kit's instruments. |
| `fiddle_list_plugins` | Known plug-ins with UIDs, instrument/effect compatibility and whether each loaded; `scanning` while a scan runs. |
| `fiddle_list_maps` | Fiddle's expression and percussion maps and how many patches and layers use each; percussion maps can be filtered by an instrument they cover. |
| `fiddle_inspect_library_sources` | Expression- or percussion-map metadata in `.doricolib` files (a given folder, or the folders Dorico loads libraries from), whether Fiddle has each map, and matching instrument plug-ins. |
| `fiddle_list_libraries` | Libraries with patch counts, patches awaiting a preset capture, and layer counts. |
| `fiddle_get_library` | One library's patches in full. |
| `fiddle_find_patches` | Patches across every library that could play a chair or instrument: the same instrument, a variant, or a percussion map that covers it (`matchedBy`). Also by words or player. |
| `fiddle_get_library_setup` | Guided-setup progress for one library. |
| `fiddle_get_chairs` | Chairs (including empty ones) with instrument, role, MIDI port and channel, kit, layers and bridge-map check (`bridge`); kits; and whether Dorico's playback template is out of date. Filter by words, chair, or chairs without layers; a filtered answer lists only those chairs' kits. |
| `fiddle_get_bridge_maps` | Whether Dorico plays the chairs: installed bridge-map sections and available built-in updates, chair counts by bridge state, and the chairs that need attention, with what to do. |
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
| `fiddle_update_layers_from_library` | Reload a layer (and its kit's linked copies), or every layer using a patch, from the library patch, as Update from Library does. | project |
| `fiddle_update_bridge_maps` | Use this Fiddle's built-in bridge maps for one section or every section with an update. | project |
| `fiddle_set_patch_preset_with_user` | Open a patch's player window, ask the user to choose its preset there, and capture it. | library |
| `fiddle_open_patch_player`, `fiddle_get_patch_player`, `fiddle_capture_patch_player`, `fiddle_close_patch_player` | The same steps one at a time, for clients that can't ask the user. | library (capture) |
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
removal and updating, and template installation are destructive; setters are idempotent.
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
   editor, give `expected_preset_name`, then `fiddle_set_patch_preset_with_user`
   for each patch: the user chooses the preset in the window Fiddle opens and
   the agent captures it (see below). `fiddle_open_guided_library` does the
   same for a whole library inside Library Manager.
5. **Create chairs.** `fiddle_get_chairs` for what exists, then
   `fiddle_create_chairs` (for example the ensemble in [Setup goal](Setup%20goal.md))
   and `fiddle_create_kit` for percussion kits.
6. **Add layers.** `fiddle_add_layer` for each chair and patch.
7. **Install the template.** If `fiddle_get_chairs` reports
   `templateStatus.dirty`, ask the user to quit Dorico, then
   `fiddle_install_playback_template`. The user restarts Dorico and applies
   the Fiddle template in open projects. `templateStatus.projectCopy` means
   the chairs differ from the installed template but match the one the open
   Dorico project was saved with: that project needs no reinstall, and
   installing would change the template other projects get.
8. **Check that Dorico plays it.** `fiddle_get_status` shows whether Dorico is
   connected. Have the user start playback in Dorico, then
   `fiddle_get_bridge_maps`: each chair with a bridge map reports when
   playback starts, and the chairs needing attention say what to do.

A chair's `bridge.state` is the check the Chairs window shows: `playing`;
`differentVersion` (Dorico has an older or edited copy of the map, so Fiddle
mutes the chair: reinstall the template and reapply it); `notInProjectTemplate`
(Dorico sent other chairs their maps but nothing to this one: restart Dorico
if the template is new, then reapply it in the project); `checkOnPlayback`
(not yet checked; play to check); or `unchecked` (the map sends no version
ID, so the chair is muted until the user verifies a Dorico export in the
Chairs window). `notesNotPlayed` counts notes Fiddle withheld, by reason; a
chair with withheld notes needs attention even when its map checks out.

When a patch's preset is captured again, layers that use it keep their old
player state until `fiddle_update_layers_from_library` with the patch ID.

## Choosing presets with the user

Many players (Synchron Player, Kontakt) choose presets only in their own
editor, and don't tell the host which preset is loaded. An agent can't pick
one, but it can do everything around it:

1. `fiddle_set_patch_preset_with_user` opens the patch's player in a Fiddle
   window, starting from its saved state. The window is outside the mixer,
   the audio graph and project versions, in a host of its own
   (`agentPatchPlayers_`), so Library Manager never closes it.
2. Once the player has loaded, the tool asks the user, through the MCP
   client, to choose the patch's expected preset in that window. On the
   2025 protocol it sends an `elicitation/create` request and waits; on
   2026-07-28 it returns `input_required` and the client calls it again with
   the answer.
3. When the user accepts, Fiddle captures the window's state into the patch,
   marks it finished and closes the window. Declining or cancelling closes
   the window without saving.

A capture is refused if the player hasn't changed since its window loaded,
the same rule as Library Manager's **Capture & continue**, so a default or
stale state is never recorded as the user's choice. If the right preset was
already loaded, the user ticks "The preset was already loaded"
(`allow_unchanged`). Fiddle can tell that the state changed, not which preset
the user chose: the question names the expected preset, and the user is the
judge.

Each window has a random session ID, and the answer carries it (through
the 2026-07-28 `requestState` on a retry). Closing the window by hand ends its
session. Fiddle refuses an answer for a window that has since closed or
reopened, including across a restart of Fiddle, and treats a repeated answer for a capture
that already happened as done rather than capturing again. A window also
records the patch as it was when it opened (`agent::patchBaseline`: saved
content, player, expected preset and whether it is finished): if any of these
changes elsewhere (Library Manager, `fiddle_save_library`) before the
capture, the capture is refused and the window has to be opened again. The
adapter likewise never asks about, or captures into, a window that replaced
the one it opened. Capturing the same state into
a finished patch succeeds without adding an Undo step.

A client that can't ask (no elicitation capability) gets `waitingForUser`
with instructions: the agent asks in chat, then calls
`fiddle_capture_patch_player`. If capture fails, the window stays open so
the agent can try again. Layers already using the patch keep their earlier
player state until the user presses **Update from Library** in each layer's
details; the capture result says how many there are.

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
- Setup edits (libraries, chairs, kits, layers, bridge maps, the template), and Undo and
  Redo in either history, are refused while Dorico is playing. The adapter tells agents not to change anything
  else during playback unless the user asks.
- Library edits are refused while Library Manager has unsaved edits, because
  saving that draft would overwrite them. After an agent edit, an open,
  unedited Library Manager editor reloads.
- Deleting a library, or removing a patch, is refused while a layer uses it.
- Deletions, updating layers from the library (it replaces changes made to
  their player in the mixer) and template installation are annotated destructive, so MCP
  clients ask before running them. The tool descriptions tell agents to
  present a plan before creating libraries or several chairs, and to have the
  user quit Dorico before installing the template.
- Installing the playback template writes Dorico's user library and isn't
  undoable; it is the same file the Chairs window's Install button writes, and
  installing again after an undo puts Dorico back in step.
- Not exposed: saving or checking out project versions, deleting maps, audio
  device and printing settings, and choosing a preset inside a plug-in's
  editor. An agent opens the player window and asks; the user chooses.

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
