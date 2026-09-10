# Undo/redo repair status

9 September 2026. First implementation checkpoint; the broader inventory is
**not yet fully repaired**. The user smoke-tested this checkpoint without finding
issues. No running server, installed
plugin, Dorico project, or user library was changed during verification.

## Implemented in this checkpoint

- History uses state identities rather than stack depth for the save point.
  Save, Undo and Redo break coalescing; every accepted new edit invalidates Redo.
  Trimming the 100-command history cannot make an unrelated state appear saved.
- Explicit range-pointer/key gestures and numeric commits bracket coalescing.
  A paused drag remains one step; separate drags do not merge. Compound gain
  commands must match exact targets, not merely the number of strips.
- The main mixer sends a single command containing all affected values for
  chair master gain, locked-chair compensation, selected-layer gain/mute/solo,
  and library activation. Clear Solos is one command across layers and buses.
  Typed/reset layer gain uses the same compensation path as dragging.
- Playback delay and chair level locks are stored in project/version state,
  the session database, and host-state blobs. Older versions without these
  fields default to 1000 ms and unlocked chairs. Default settings preserve
  existing state hashes. Undo/Redo also updates session persistence and the UI.
- The mixer has Undo/Redo buttons with action descriptions. Keyboard handling
  recognizes shifted Z and leaves focused text fields' Undo to the editor.
  Library-window shortcuts no longer fall through to the project's history;
  the separate library implementation is still pending.
- Rejected/no-op commands can explicitly report that status to the manager.
  New mixer controls and chair-layer add/remove use this seam; compounds roll
  back completed children when a subsequent child rejects. Older actions still
  need migration and failure-path tests; this is not a universal transaction
  guarantee for every command or asynchronous plugin load.
- Detected vendor instrument/FX edits advance an external revision, so undoing
  a Fiddle command cannot falsely clear their dirty status. Individual vendor
  gestures remain outside Fiddle Undo as agreed.

The previously implemented chair-layer add/remove and audio-safety work remains
in the working tree; see [audio safety results](audio-safety-results.md).

## Verification

- Release server and all stable test targets built.
- Stable CTest suite: 53 entries, including the new shared-history and gesture
  tests. Harmonic-analysis tests remain outside the stable suite.
- Mixer integration exercises atomic compensated changes, exact target
  identity, rejected batches/compound rollback, project settings Undo/Redo,
  SQLite close/reopen, host blob exchange, and actual project restoration.
- State-deserializer tests cover optional project settings, legacy defaults,
  malformed/truncated settings, bounds and quoted chair IDs.
- Isolated rendered-browser test exercises all three strip densities, one
  compensated mute command, authoritative gain display after Undo, disabled
  Redo, delay key-gesture bracketing, text-local Undo and shifted-Z Redo.
  It uses a fixture bridge, not the live server; native command/model tests
  separately cover execution and persistence.

## Still to implement

1. Chair create/delete/name/role commands, including atomic batch role edits,
   retained child players and exact MIDI assignments on Undo/Redo.
2. Complete plugin state retention for VSTi replacement/program selection and
   repeated FX add/remove cycles; expression-map provenance/import and Lua
   loading failure handling. Preserve vendor edits without recording their
   individual editor gestures.
3. Update Layers as one project command with each layer's live before-state.
4. Separate Library Manager history: draft edits, row operations, batches,
   preview-player state, save checkpoints and recoverable catalog changes.
   History lifetime across closing/restarting still needs an explicit policy.
5. Remaining legacy endpoints, no-op/failure handling and gesture regressions,
   plus native menu integration where appropriate.

## Short manual check after restarting the rebuilt Release server

1. Save a project. Change Delay, then Undo/Redo. Confirm the numeric value and
   audible delay follow; Undo to the saved state should disable Save.
2. Lock a multi-layer chair. Mute a layer, then Undo once: both mute and the
   compensating gains should revert. Repeat with a chair master fader drag.
3. Save with non-default Delay and a locked chair; reopen via Dorico and check
   that both settings return. Save a second version with different values and
   verify that restoring the first version restores its settings.
4. Edit a vendor plugin, then change and undo a Fiddle gain. Save should remain
   enabled for the plugin edit. Text-field Command-Z should not change the mix.

No native Fiddle plugin rebuild/install is required for this checkpoint's
additive host-state extension. Continue using the Release server.
