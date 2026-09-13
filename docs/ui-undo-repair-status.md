# Undo/redo repair status

Updated 13 September 2026. The first three checkpoints are committed and
smoke-tested; the layer-refresh follow-up below also passed automated testing
and the user's smoke check. The broader inventory is
**not yet fully repaired**. Verification used isolated fixtures.

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
  the separate library implementation is described in the latest checkpoint below.
- Rejected/no-op commands can explicitly report that status to the manager.
  New mixer controls and chair-layer add/remove use this seam; compounds roll
  back completed children when a subsequent child rejects. Older actions still
  need migration and failure-path tests; this is not a universal transaction
  guarantee for every command or asynchronous plugin load.
- Detected vendor instrument/FX edits advance an external revision, so undoing
  a Fiddle command cannot falsely clear their dirty status. Individual vendor
  gestures remain outside Fiddle Undo as agreed.

The chair-layer add/remove and audio-safety work is included in `6866ac4`;
see [audio safety results](audio-safety-results.md).

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
2. Remaining legacy endpoints, no-op/failure handling and gesture regressions,
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

The first checkpoint was committed as `6866ac4`. The subsequent
chair-management pass, committed as `63e947c`, adds:

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

10 September 2026; smoke test passed, committed as `1ccd4c9`.

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

## Layer-refresh follow-up

13 September 2026; resumed after the usage-limit interruption. Release build,
automated tests and the user's smoke check passed. Included in the checkpoint
"Make library layer updates undoable as one project edit".

- Library Manager's Update Layers and the strip's Refresh Library share
  `LayerLibrarySetupAction`. A batch is one project-history step. It updates
  all linked layers and captures each layer's independent live player state,
  expression-map object/import path, patch revision and edited flag.
- Undo/Redo captures departing player state on each cycle and does not consult
  the current catalog. Gain, mute, solo, activity, routing, position, Lua and
  effect racks remain outside the fields replaced by the refresh.
- All target rows are preflighted and their setup fields committed in one SQL
  transaction before live presets change. Missing/reassigned/duplicate targets
  or a database failure reject the command without advancing history. Failed
  Undo retains its history entry so it can be retried.
- Pending or missing players retain their serialized state. Immediate Undo
  cancels superseded loads. Empty instrument setups are reversible. Bypass
  changes made during a library-triggered load survive its completion.
- Update Layers needs one click. Library linkage counts refresh after project
  Undo/Redo without replacing unsaved library drafts. The result identifies the
  main mixer as the place to undo the operation. Library edits themselves still
  await their separate history implementation.

Verification: Release FiddleServer and FiddleTests built. All 53 stable CTest
entries passed, with the two socket tests rerun with localhost permission after
the sandbox blocked binding. The old source-contract test was updated for the
shared action name. New repository tests force a second-row SQL error and verify
rollback and unchanged mix fields. Mixer tests cover failed Execute/Undo, distinct
presets, imported maps, repeated edits, pending/missing/empty players and bypass
changes during loading. The rendered Chrome fixture verifies one-click dispatch,
disabled states, count changes after Undo and preservation of dirty library drafts.

Smoke check: restart the rebuilt Release server, update a library patch used by
two layers with different presets, save/reopen the library, then click Update
Layers. Undo once in the mixer should restore both individual presets; Redo
should reapply the library setup. Check that levels/routing/FX stay unchanged
and that the Library Manager's update availability follows Undo/Redo. Also try
Refresh Library on one layer. A large player may need time to reload. This pass
does not require rebuilding or reinstalling the native Fiddle VST3.

## Library Manager history checkpoint — 13 September 2026

Implemented and automatically verified. User approved the checkpoint ("lgtm")
after the smoke-test handoff on 13 September 2026.

- The editor has **Undo draft / Redo draft**. Add/duplicate/delete, classification,
  character, expression maps, player assignments, reordering and sorting are
  undoable. An ensemble or batch assignment is one step. Text editing keeps
  native text undo while focused and becomes one draft edit on blur/Enter.
- Player setups use distinct retained preview identities. Undoing replacement
  or deletion recovers the old live instance, including unsaved vendor settings;
  duplicates capture the source setup at duplication time. Loading, unavailable
  and empty-state setups are retained too. Individual vendor editor gestures
  remain the vendor's responsibility, not extra Fiddle history entries.
- Save stays in the editor and establishes a checkpoint only after native
  success. Failure keeps the draft and history. Active loads block Save with
  a retry message. New empty libraries can be saved.
- The library list has **Undo catalog / Redo catalog** for saves, creation and
  deletion. This is a separate native history from project Undo. Header and
  patch writes/deletion are atomic; referenced-patch removal is rejected on
  Execute, Undo and Redo without advancing history. Exact saved revisions are
  restored, with a durable revision high-water mark preventing reuse on a new
  branch. Migrated legacy rows are retired when a modern catalog edit succeeds,
  preventing emptied libraries from resurrecting those rows on restart.
- Project linkage counts remain live during draft Undo/Redo. Update Layers is
  still a **project** edit and is undoable in the main mixer, not either library
  history. Catalog edits never restore layer/mixer state.
- The patch table scrolls horizontally at smaller window sizes instead of
  clipping its right-hand actions; the browser fixture checks the default size.
- Closing a dirty editor asks before discarding. Quit/Restart also asks about
  an unsaved library draft. Closing the Library Manager's native window only
  hides it, retaining the draft. Draft history ends on closing/discarding the
  editor; catalog history ends on server exit. Both histories are bounded to
  100 entries. Preview instances no longer reachable from history are released.

Verification: Release server/tests build, 54 stable CTest entries, and rendered
Chrome fixture. Coverage includes independent history, reference guards,
transaction rollback, revision branching, retained/duplicated/pending player
state, text commits, batch/ensemble edits, failed-save acknowledgement, saved
checkpoints, discard confirmation and absence of project Undo dispatch.

Smoke check: restart the Release server; open a library and change a real
player's preset. Delete its row, Undo draft, and reopen the player to verify
the preset remains. Try replacing its player and Undo/Redo. Save, then Undo
and Redo a draft edit, and verify the unsaved indicator follows the checkpoint.
Close the editor and try Undo catalog / Redo catalog. A scratch unused library
can exercise deletion/restoration. Check Quit/Restart's unsaved-draft warning.
No native Fiddle VST3 reinstall is required.
