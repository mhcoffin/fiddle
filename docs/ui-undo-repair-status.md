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

1. VSTi program-selection Undo; expression-map provenance/import and Lua
   loading failure handling. Preserve vendor edits without recording their
   individual editor gestures.
2. Update Layers as one project command with each layer's live before-state.
3. Separate Library Manager history: draft edits, row operations, batches,
   preview-player state, save checkpoints and recoverable catalog changes.
   History lifetime across closing/restarting still needs an explicit policy.
4. Remaining legacy endpoints, no-op/failure handling and gesture regressions,
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

## Chair-management follow-up

The first checkpoint was committed as `6866ac4`. The subsequent, uncommitted
chair-management pass adds:

- Undo/Redo for creating, deleting, renaming and changing a chair's player type.
  Batch role corrections are one command and one database transaction.
- Exact IDs, MIDI destinations, ordinals and placement on restoration; undoing
  deletion releases the matching MIDI tombstone so new chairs cannot reclaim
  an occupied destination.
- Deleted chairs retain their live layers, instrument/FX instances, direct
  output assignments, current mixing controls and chair lock. Pending player
  loads may complete while retained by history. Deletion closes player/FX
  windows and clears held/future notes; Undo does not resurrect old notes.
- Metadata edits update only chair metadata on live strips, never stale layer
  controls or catalog presets. Invalid batches roll back without a history step;
  unchanged edits neither dirty the project nor add history.
- Chair Manager has project Undo/Redo buttons with action descriptions. Save,
  version state and playback-template-dirty status update in both directions.
  Installing a template in Dorico remains outside Undo; Undo only updates the
  local definition and installation-needed indicator.

Offline integration tests cover repeated create/delete/restore, layer ordering
around unrelated strips, role/ordinal conflicts, live FX/gain/audio preservation,
missing-catalog references, pending loading, lock restoration, rejected edits,
and version identity after renaming/undoing empty chairs. Repository tests force
failures halfway through batch updates and child restoration to verify rollback.
The browser fixture exercises rename, role, delete and Undo UI dispatch and the
restored name; it is not a claim of a live Dorico end-to-end test.

Manual check after restarting Release: add an empty chair and Undo/Redo; rename
and change its player type and Undo each; delete a populated chair and Undo once.
Check its layers, levels, lock, MIDI destination and playback are restored. The
chair-management smoke test passed; committed as `63e947c`. No push was requested.

## Plugin-state follow-up

10 September 2026; uncommitted, awaiting smoke test.

- VSTi replacement/removal retains full plugin description, serialized state
  and bypass state on both sides. Every direction captures the departing
  state again, preserving vendor edits made after an earlier Undo or Redo.
  Restoration does not depend on the current scanner catalog. Selecting an
  already assigned working player is a no-op; retrying a missing player retains
  its patch state.
- Strip, group-bus and Master FX add/remove commands capture live state before
  every removal. Redo of Add no longer restores an empty/default effect, and
  repeated Remove/Undo cycles do not revert later vendor edits.
- Pending instrument and FX restores retain their input state before loading
  completes. Superseded load callbacks cannot replace the current choice;
  missing/incompatible players retain state for recovery. FX bypass edits made
  while loading survive completion.
- The processor installation and restored-state application share one control
  gate, preventing the audio renderer from briefly processing a default preset.
  The audio thread does not wait for that gate. Plugin completion after Undo
  respects the history save point instead of unconditionally enabling Save.

These commands restore serialized plugin state by **reloading** the plugin;
large sample libraries can take time. This is not retention of sample-engine
runtime/tails, nor recording individual vendor-editor gestures. Chair deletion
still retains live instances as documented above.

New deterministic mixer tests cover edits on both sides of VSTi replacement,
repeated add/remove FX cycles on all three destinations, removal during pending
restoration, stale completions, missing binaries, catalog changes, no-op/invalid
selection and bypass changes while loading. State values are asserted directly;
tests require no vendor plugins or Dorico.

Smoke check after restarting Release: configure a VSTi, clear/replace it, Undo,
and verify the patch returns. Edit it again and repeat Redo/Undo. For a strip,
bus and Master FX, edit a parameter, Undo Add and Redo; then remove/Undo, edit
again, and Redo-remove/Undo. The latest settings should survive each cycle.
