# UI gesture and undo/redo inventory

Audit of the working tree, 9 September 2026. Includes the recent, uncommitted
chair-layer add/remove fixes and audio-safety work.

This is the **baseline audit**, not a claim that all findings remain unchanged.
See [repair status](ui-undo-repair-status.md) for implementation and test results.

## Conclusion

Fiddle uses a unified **Command pattern**, but its use is incomplete and some
commands do not faithfully reverse the edit. It is not yet reasonable to claim
that project editing has reliable, comprehensive Undo/Redo.

The shared `UndoManager` runs `UndoableAction::execute()` / `undo()`, maintains
two stacks, limits history to 100 actions, and coalesces adjacent matching IDs
within 500 ms. `CompoundAction` combines sub-actions. This is Fiddle's own
implementation, not JUCE's `UndoManager`. Command services, direct JS handlers,
frontend-only changes, and vendor editors currently coexist.

This audit traced 104 distinct literal `dispatchCpp` message names in current
Svelte/JS sources, their native handlers, the command implementations, and
local-only gestures. The count includes queries and legacy helper functions;
it is not a count of 104 visible buttons or undoable edits. Native menus and
vendor-editor boundaries were reviewed separately. No live score, library,
running server, or installed plugin was modified during this audit.

## Policy decisions

Agreed with the user during the audit:

- Library Manager edits get a **separate library undo history**, not the score's
  project history.
- The chair level-lock toggle becomes a **saved, undoable project setting**.
  Today it is transient frontend state.
- **Update Layers** from a library is one project undo step, like a compound
  single-layer refresh. It does not undo the library edit that preceded it.
- Save, branch/history operations, playback-template installation and local
  audio-device settings remain outside ordinary project Undo.
- Playback delay is a **saved, undoable project setting**, not a device or
  temporary UI preference (confirmed after the initial audit).

Also agreed: individual gestures inside third-party plugin editors stay with
the plugin's own Undo, while Fiddle's add/remove/replace/refresh commands
preserve complete plugin state. Arbitrary
editor gestures cannot be assumed to expose reliable host-side transaction
boundaries. Version snapshots already provide a separate full-state recovery
mechanism; that is not equivalent to gesture Undo.

Library follow-up detail: use draft-level undo while editing a library; treat
Save as a library-history checkpoint, and retain recoverable committed edits
in that separate domain. Exactly how long library history survives closing the
editor/restarting still needs specification. Text fields should retain normal
text Undo while focused; they must not silently undo a score edit.

## Verified shared defects — fix these first

The first six findings below were reproduced with an isolated C++ probe using
the real undo classes and production mixer objects, deterministic fake plugins,
and existing offline test fixtures. No Dorico or installed vendor plugin was
used. Keyboard findings were reproduced by evaluating the actual App handler
with synthetic keyboard events. Other entries are source-traced, not claimed
as full interactive acceptance tests.

| Finding | Evidence / consequence | Required correction |
|---|---|---|
| Saved-state identity is only stack depth | Save A=-1/B=0, Undo, edit B=-2: A=0/B=-2 is reported at the save point. After 100 actions, a further edit that trims history also reports clean. | Track stable history state IDs/revisions, not an index. Retained history trimming must preserve or invalidate the saved identity correctly. |
| Coalescing crosses Save | Set gain -1, Save, set -2 quickly: manager reports clean; Undo goes to 0 rather than the saved -1. | Save is a coalescing barrier. |
| Coalescing leaves stale Redo | Edit A, edit B, Undo B, edit A quickly: `canRedo()` remains true. | Every new edit invalidates Redo, including the coalescing path; Undo/Redo are coalescing barriers too. |
| Group coalescing ignores target identity | Group delta on A, then another on B: one Undo restores A instead of B; Redo applies B's new value to A. | Match exact target identities, operation type and gesture identity. Do not merge sub-actions merely by list position/count. |
| Strip mute/solo bypass history | Starting with empty history, both setters succeed and `canUndo()` remains false. | Commands for single and selected-set mute/solo, plus one command for Clear Solos. |
| Plugin state is not preserved by some structural commands | VSTi removal followed by Undo restores fake-player default 0.9375 instead of its prior 0.1. Add FX, configure 0.25, Undo/Redo restores default 0.9375. | Keep the original live instance where appropriate, or capture both complete before/after states at the correct lifecycle boundaries. |
| Keyboard Undo hijacks text editing | Cmd-Z with an INPUT target prevents default and dispatches project `undo`. This also happens in the Library Manager. | Focus-aware dispatch: text first, active library/project domain second. |
| Redo shortcut is case-sensitive | Handler accepts only `e.key === "z"`; a Shift-Z event with `key: "Z"` is ignored. | Normalize key case and test actual shortcuts in each window. |
| Failure cannot be reported to history | Actions return `void`; services often return success after `perform()` regardless of async load failure or an action's early return. New layer actions also inherit this limitation. | Explicit result/pending lifecycle, no history step for rejected/no-op operations, defined recovery for partial compound failures. |
| Fader gestures have no transaction boundaries | 500 ms timing merges separate quick drags/typed values and splits one drag that pauses. Multi-target chair gestures send many separate strip commands. | Explicit begin/update/end gesture or atomic final-state command; keyboard stepping and typed commit need defined boundaries. |
| Dirty tracking includes out-of-history edits | Plugin editor changes and direct setters mark dirty, but Undo's handler can unconditionally clear dirty solely on `isAtSavePoint()`. | A history save marker cannot override other persistent changes. Validate canonical state or track non-command edits too. |
| UI gain shadows outlive Undo | `gainShadowRaw` is written by local gestures but never reconciled by authoritative mixer-state/Undo updates. Chair totals and later compensation read it. | Rebase shadows on authoritative revisions, retaining only explicitly pending gesture values. Test visible totals and subsequent edits, not just C++ gains. |

Sources: [UndoManager](/Users/mhc/fiddle/Source/Server/UndoManager.h),
[actions, including CompoundAction](/Users/mhc/fiddle/Source/Server/UndoActions.h),
[mixer command service](/Users/mhc/fiddle/Source/Server/MixerCommandService.cpp),
[strip FX actions](/Users/mhc/fiddle/Source/Server/StripAudioActions.h),
[keyboard handler](/Users/mhc/fiddle/Source/Server/ui/src/App.svelte:338),
[mixer gesture implementation](/Users/mhc/fiddle/Source/Server/ui/src/lib/MixerPanel.svelte:435),
[undo dispatch and dirty clearing](/Users/mhc/fiddle/Source/Server/MainComponent.cpp:4551).

## Gesture inventory

Legend: **Connected** means an action exists, not that every edge case has been
tested. **Partial** means an identified correctness or transaction problem.
**Gap** means no appropriate Undo. **Excluded** means not project Undo under the
chosen policy. All connected actions are affected by shared-engine defects above.

### Main mixer, chairs and layers

| User gesture / message | Current status | Details / intended behavior |
|---|---|---|
| Add a layer: `assignPatchToChair` | Connected; recent regression tests | Creates once; Undo detaches assignment and live strip; Redo retains ID, player/state, FX, order and routing. Tested ordinary/pending/missing players and interleaved removal. |
| Delete a layer: `removeChairLayer` | Connected; recent regression tests | Retains actual strip and assignment; persists and updates chair UI on both directions. Selected chair-layer delete currently affects the clicked layer, not the whole selection. Any batch-delete UX should be explicit and one command. |
| Refresh layer: `refreshChairLayerFromLibrary` | Connected; needs stronger acceptance | Before/after LayerRows, with live instrument bytes captured before refresh. Test missing-plugin paths, unsaved edits and pending loads. Verify latest live state on repeated undo/redo, not merely patch revision. |
| Ordinary fader, fine Shift-drag, arrow stepping: `setStripGain` | Partial | Value reversal exists; temporal coalescing is not a reliable gesture boundary. |
| Typed gain / double-click 0 dB: `setStripGain`, `setGroupGainAbsolute` | Partial | Actions exist; typed/reset commits should be discrete; must use same locked-chair semantics as dragging. Current reset/typed path differs from the locked drag path. |
| Chair total fader / double-click reset | Partial | Frontend loops over individual `setStripGain` messages. One visible gesture becomes multiple undo steps; alternating strip IDs prevent useful drag coalescing. |
| Locked-chair layer gain adjustment | Partial | Compensation and target gain are separate actions; Undo can leave an intermediate, unbalanced mix. All affected values belong in one atomic command. |
| Multi-selection group gain: `setGroupGainDelta`, `setGroupGainAbsolute` | Partial | Compound commands exist; delta coalescing has the reproduced wrong-target bug. Current slider `oninput` goes through `handleGroupFaderInput`, not the older `handleFaderInput` multi-selection helper; typed gain uses the selection-aware path. Align intended selection behavior as well as undo. |
| Mute / selected-set mute: `setStripMute` | Gap | Direct model setter. Locked mute additionally creates separate sibling-gain actions without undoing mute itself. |
| Solo / selected-set solo: `setStripSolo` | Gap | Direct model setter. |
| Clear Solos | Partial | Loops over non-undoable strip setters and separate undoable bus setters; not one reversible operation. |
| Activate/deactivate library chip: `toggleLibraryActive` | Partial | Active states are captured by one action; locked-chair gain compensation is dispatched separately. One Undo does not undo the whole visible gesture. |
| Chair level-lock icon | Gap; policy agreed | Frontend-only `groupMasters[key].lockSum`; not persisted or undoable. Add to project state/versioning and command history. |
| Create chair: `createChair` | Gap | Direct repository insert and template-dirty update. Undo must preserve stable MIDI destination, role, ordinal and placement on Redo. |
| Rename / change solo-section role: `updateChair` | Gap | Direct repository mutation and synchronization. Name/role commit should be one action with exact old/new chair state. |
| Batch chair roles: `updateChairRoles` | Gap | Direct loop; failure can leave a partial edit. One atomic command for the selected set. |
| Delete chair: `deleteChair` | Gap | Deletes chair and all layers directly. Undo must retain child instances, FX, assignments and stable port/channel allocation. Older layer history can fail if its chair has been deleted outside history. |
| Playback delay slider / typed milliseconds: `setPlaybackDelay` | Gap / policy to specify | Direct model assignment; not found in project/version serialization either. It affects audible timing and Lua budget. Recommend making it a persisted project edit, with gesture boundaries and the existing safe render-ahead reconfiguration behavior. |

Sources: [MixerPanel](/Users/mhc/fiddle/Source/Server/ui/src/lib/MixerPanel.svelte),
[ChairManager](/Users/mhc/fiddle/Source/Server/ui/src/lib/ChairManager.svelte),
[chair/layer handlers](/Users/mhc/fiddle/Source/Server/MainComponent.cpp:3444),
[chair-layer actions](/Users/mhc/fiddle/Source/Server/ChairLayerActions.h).

### Layer Details / instrument and MIDI setup

| Gesture / message | Current status | Details |
|---|---|---|
| Set, replace or clear VSTi: `setStripPlugin`, `setGroupPlugin` | Partial | `SetPluginAction` records UIDs only. Undo can reload an empty/default player and lose the old patch. Batch wrapper exists, but state/lifecycle semantics need repair. |
| Choose VSTi program: `setStripProgram` | Gap | Direct `setPluginProgram`; undo should preserve full state if selecting a program resets parameters. |
| Choose / clear xmap: `loadExpressionMap`, `clearExpressionMap`, `setGroupExpressionMap` | Partial | Commands exist, including compound batch assignment. Undo reloads the old entity from today's catalog and discards the file path; cannot faithfully restore an imported/non-catalog map. Retain old/new map data and provenance. |
| Import xmap file: `loadExpressionMapFromFile` | Gap | File chooser/parsing is external/read-only; assigning the parsed result is a project edit but directly mutates the strip. Cancel should create no history. |
| Add/remove Lua processor: `addStripLuaPlugin`, `removeStripLuaPlugin` | Connected with qualifications | Undo/Redo reloads script files by name rather than retaining source/runtime; changing/deleting a file changes behavior. Failed loads can still occupy history. Define whether source contents or only references belong to project state; do not promise restoration of arbitrary evolving Lua runtime state. |
| Change direct output: `setStripDirectOutput` | Connected | Records old/new bus ID. Needs interleaved bus/layer deletion tests and exact persistence verification. |
| Edit/hide instrument: `showStripEditor`, `toggleStripEditor` | Excluded | Window visibility is not a musical edit; preserve normal plugin-editor-local keyboard handling. |
| Open/hide MIDI inspector | Excluded | View state; it does not change routing. |
| Free-standing strip library label: `setStripLibrary`, `setGroupLibrary` | Connected; legacy UI | Editable only for non-chair strips. Batch action exists. Chair layer's library label is read-only. |
| Free-standing strip duplicate: `duplicateStripInput` | Partial; legacy UI | Redo recreates from current source subset, not the retained duplicate. Needs same lifetime/state discipline as chair layers. Hidden for chair layers. |
| Free-standing add/delete/group delete: `addMixerStrip`, `removeMixerStrip`, `removeGroupStrips` | Partial; legacy helpers | Commands exist; Add redo makes a blank strip, unlike new chair-layer add. Keep-alive removal is better. Old endpoints must not mutate chair layers without their repository rows. |
| Input port/channel: `setStripInput` | Connected; no current visible control found | Legacy helper/action. Chairs own current routing; a stray per-layer override would disagree with the chair model. Remove/restrict legacy route-changing entry points. |

Sources: [LayerDetailsPanel](/Users/mhc/fiddle/Source/Server/ui/src/lib/LayerDetailsPanel.svelte),
[plugin service](/Users/mhc/fiddle/Source/Server/PluginCommandService.cpp),
[expression-map service](/Users/mhc/fiddle/Source/Server/ExpressionMapCommandService.cpp),
[shared actions](/Users/mhc/fiddle/Source/Server/UndoActions.h).

### Audio buses, Master, and FX racks

Large-view buttons and the generously sized Audio FX panels use the same
underlying services; multiple entry points still need equivalence tests.

| Gesture / message | Current status | Details |
|---|---|---|
| Add bus, optionally routing selected strips: `addGroupBus` | Connected; integration-tested | One command captures prior routes; retains bus across Undo/Redo. |
| Remove bus: `removeGroupBus` | Connected; integration-tested | Restores bus, position and affected routes. |
| Rename / reorder bus: `renameGroupBus`, `moveGroupBus` | Connected | Native actions exist; add UI commit/Undo/Redo tests, including no-op moves/names. |
| Bus gain: `setGroupBusGain` | Partial | Scalar command works; shared coalescing/savepoint defects apply to drag/typed/reset. |
| Bus mute/solo: `setGroupBusMute`, `setGroupBusSolo` | Connected | Unlike strips, these already have commands. Clear Solos still needs a single compound operation. |
| Master gain: `setMasterGain` | Partial | Scalar command; same gesture/savepoint issues. |
| Add layer FX: `addStripInsert` | Partial | Add action snapshots only initial description/empty state. Redo loses later vendor edits (reproduced). |
| Add bus/Master FX: `addGroupBusInsert`, `addMasterInsert` | Partial | Same initial-snapshot design as layer FX; no capture before undo removal. |
| Remove FX: `removeStripInsert`, `removeGroupBusInsert`, `removeMasterInsert` | Connected with state caveat | Captures state in action constructor for first Undo. If an effect is edited after Undo, Redo-remove/Undo can restore the original stale snapshot rather than the newly edited state. Define and test retention through repeated cycles. |
| Reorder FX / move pre↔post: `moveStripInsert`, `moveGroupBusInsert`, `moveMasterInsert` | Connected | Old/new rack/index captured; test move across racks, unchanged/clamped positions, pending loads and repeated Undo/Redo. Master has a single rack. |
| Bypass/enable FX: `setStripInsertBypassed`, `setGroupBusInsertBypassed`, `setMasterInsertBypassed` | Connected | Boolean actions exist. No-op requests should not consume history or dirty the score. |
| Edit/hide FX: `showStripInsertEditor`, `toggleStripInsertEditor`, `toggleGroupBusInsertEditor`, `showMasterInsertEditor`, `toggleMasterInsertEditor` | Excluded | Visibility only; individual vendor edits are a separate boundary, not automatically covered by these commands. |

Sources: [strip actions](/Users/mhc/fiddle/Source/Server/StripAudioActions.h),
[bus actions](/Users/mhc/fiddle/Source/Server/GroupBusActions.h),
[Master actions](/Users/mhc/fiddle/Source/Server/MasterAudioActions.h),
[bus panel](/Users/mhc/fiddle/Source/Server/ui/src/lib/BusAudioPanel.svelte),
[layer panel](/Users/mhc/fiddle/Source/Server/ui/src/lib/ChannelAudioPanel.svelte),
[Master panel](/Users/mhc/fiddle/Source/Server/ui/src/lib/MasterAudioPanel.svelte).

### Library Manager — separate history

The table below records the original audit. The September 13 implementation
now provides editor-local draft history and separate native catalog history;
see [repair status](ui-undo-repair-status.md#library-manager-history-checkpoint--13-september-2026)
for tested behavior and lifetime policy. Update Layers is project history.

| Gesture | Current behavior / intended treatment |
|---|---|
| Create a library; edit library name/vendor/variant | Draft until `saveLibrary`; separate library commands/checkpoint. |
| Add blank patch / add an ensemble's patches | Direct append; one command per add/ensemble gesture, not one command per generated row. |
| Duplicate / delete patch row | Direct array change; deletion also discards preview instance (`discardLibraryPatchPreview`). Undo must retain row and any unsaved preview state. |
| Rename patch / change character | Direct bound field changes; text-local Undo followed by one committed library edit. |
| Classify / clear classification | Changes entity, instrument name and family together; one library command. |
| Reorder by drag/drop / keyboard / orchestral sort | Direct array changes; one command for each completed reorder or entire sort. |
| Set/clear player on one row / batch assignment | Direct edits discard preview state. Command must retain before/after player setup for each affected patch, not only UID. |
| Set/clear xmap on one row / batch assignment | Direct fields; one library command for selected-set assignment. |
| Preview player editor: `openLibraryPatchEditor` | Showing it is view state; vendor edits affect unsaved library setup. Preserve that setup when other library commands are undone. |
| Close/cancel library editor: `closeLibraryPatchPreviews` | Closes previews and abandons draft. History lifetime and dirty-draft confirmation need explicit UX. |
| Save: `saveLibrary` | Direct catalog replacement; excluded from project history. Proposed checkpoint in library history, with save success/failure reflected correctly in the editor. |
| Delete library: `deleteLibrary` | Direct deletion, blocked while live layers reference its patches. Recommended undoable in separate library history, retaining all catalog/player state. |
| Update Layers: `updateLayersFromLibraryPatch` | Direct bulk replacement after a two-click confirmation. Agreed: one **project** command capturing each affected layer's live before state, not a library-history command. |
| Search, pick library (`loadLibrary`), select/range-select/all rows, preview status, pending confirmation | View/navigation state, not musical/library data changes; excluded from data history. Selection defines command targets but should not itself consume undo steps. |

Sources: [LibraryManager](/Users/mhc/fiddle/Source/Server/ui/src/lib/LibraryManager.svelte:175),
[library persistence handlers](/Users/mhc/fiddle/Source/Server/MainComponent.cpp:3787),
[Update Layers](/Users/mhc/fiddle/Source/Server/MainComponent.cpp:4139).

### Excluded actions and local UI

History controls themselves, `undo` and `redo`, are not new history entries.
They currently operate the single project manager regardless of which WebView
has focus. There are no native Edit-menu Undo/Redo entries or exposed action
names/availability; add these with the focus-aware history routing.

| Gesture / message | Current behavior and boundary |
|---|---|
| Project Save: `saveConfig`, native File → Save / Cmd-S | Version/checkpoint operation, not an undoable edit. Must end gesture coalescing without destroying valid edit history. |
| Branch/version operations: `createBranch`, `checkoutBranch`, `checkoutVersion`, `mergeBranch`, `deleteVersion` | Version store, not ordinary Undo. Loading a different stored state clears history. Keep explicit recovery/warnings; do not imply Cmd-Z can recover deleted versions or reverse external publication. |
| Install Playback Template: `installChairTemplate` | Writes Dorico configuration; not undone by project Undo. Undoing a chair edit after installation must mark the template stale again, not silently alter installed Dorico files. |
| Audio settings: `showAudioSettings`, device/rate/buffer selectors in native dialog | Machine-local device configuration, not versioned project history. |
| Plugin scanning: `scanPlugins`, `rescanPlugins` | Updates local discovery cache; not a score edit. |
| Window actions: `showLibraryManagerWindow`, View menus, Restart/Quit, native Window tiling/minimize/fullscreen, panel close/open/Escape | Excluded. Must not redirect text/plugin-window Undo to an unrelated domain. |
| Mixer density, global zoom buttons/shortcuts, family/bus-bank collapse, scrolling, inspector resizing, selection/range selection | Layout/view preferences; excluded. Density/zoom are stored locally; chair lock is the explicitly agreed exception. |
| Expression-map/layer picker search, group expansion, Cancel | View state; final selection belongs to the destination's data command. Persisted search is still a UI preference, not project history. |
| Inspector: `getAnnotationRecords`, `clearAnnotationRecords` | Diagnostic history, not musical state. Clear is currently irreversible; keep it outside project Undo. |
| MIDI capture: `startMidiCapture`, `stopMidiCapture`, `clearMidiCapture`, `getMidiCapture`, `getCaptureStripList`, `exportCaptureFile` | Diagnostic session/file export. Excluded; export should not create an undo step. |
| Audio diagnostics open/close, mark glitch, clear marks, `copyAudioDiagnosticsReport` | Diagnostic UI/clipboard, excluded. |
| Debug timeline zoom/hover, fake test note, clear note history/logs; tempo/time-signature marker edits/deletion | Local display/test data, not Dorico score editing; excluded from project Undo. Tempo edits update only App's frontend `tempoChanges`. Labeling should avoid implying a score edit. |
| Duplicate-connection warning dismissal: `dismissConnectionWarning` | View acknowledgement, excluded. |
| IPC setup/logging: `signalReady`, `nativeLog` | Infrastructure, excluded. |

Read-only requests, also excluded: `requestBranches`, `requestCurrentBranch`,
`requestMixerState`, `getAvailableInputs`, `requestPluginsState`,
`requestMasterAudioState`, `requestGroupBusState`, `requestExpressionMaps`,
`requestChairs`, `requestLayerCatalog`, `requestDoricoInstruments`,
`requestStripAudioState`, `requestLibraries`, `getPlaybackDelay`.

Backend-only legacy routes found in addition to the 104 UI literals include
`saveSelectedInstruments`, `buildPlaybackTemplate`, `restoreLibraryPluginState`,
`restoreLibraryPatchState`, import-capture/setup/history helpers. These should
be retired or explicitly restricted rather than left as an alternate way to
mutate the current project outside its command model. They were not counted as
current visible gestures. Counter/example and unreferenced InstrumentList
components are not active editing surfaces.

## Implementation order and acceptance requirements

1. **Repair the shared undo engine and dispatch.** State identity/savepoint,
   redo invalidation, failure/no-op results, exact gesture boundaries, exact
   compound target identities, focus-aware shortcuts. Expose `canUndo`,
   `canRedo` and descriptive action names in an Edit menu or suitable UI.
2. **Finish project command coverage.** Mute/solo/Clear Solos; chair
   create/delete/rename/role/batch roles; persisted level lock; atomic chair
   gain/compensation/library activation. Decide playback-delay persistence.
3. **Make plugin and map reversals faithful.** Preserve instrument state,
   FX add/remove cycles, program changes, file-imported map contents/path,
   pending/missing processor states and layer-refresh metadata. Convert
   Update Layers into one project command.
4. **Separate library editing.** Library command history, draft transactions,
   row operations and preview retention; route keyboard/menu Undo by focus.
   Specify committed-history lifetime/recovery rather than silently extending
   the project version store to the global catalog.
5. **Install regression coverage at the gesture boundary.** Assert each
   persistent gesture reaches an approved command/domain, not just that its
   handler calls a mock setter. Prefer full UI→router→command tests; where
   impractical, pair browser payload tests with real native command tests.

For each persistent gesture, test: original→edit→Undo→Redo; one user gesture is
one step; untouched targets unchanged; Save/Undo/new edit; saved-state indicator;
SQLite/version restore; selection changed between gestures; repeated cycles;
failed/cancelled/no-op requests; pending plugin loads; and history discard while
audio is active. For compound gestures, compare the whole state and visible
chair total, not one fader. For retained plugins verify state bytes and rendered
audio with deterministic fake processors. Vendor acceptance remains a separate
compatibility layer.

The existing 51-entry stable suite was green before this audit, including the
recent layer add/remove tests. It did **not** cover these newly reproduced
failure cases. Green CTest output must not be taken as complete undo coverage.
This turn adds this inventory, not the production repairs listed above.

## Probe results retained for follow-up

Temporary harness: `/tmp/fiddle-undo-audit-probe.cpp`; output:
`/tmp/fiddle-undo-audit-probe.log`. It reuses the existing integration fixtures
and links the current Release production objects. It observes defects rather
than registering expected-broken behavior as a passing stable regression.

```text
saved A=-1,B=0; current A=0,B=-2; isAtSavePoint=1
edit across save coalesced; isAtSavePoint=1; undo gain=0 (saved -1)
new edit after Undo leaves canRedo=1
changed selection/group gains; undo A=0,B=-2 (expected -1,0)
redo A=-2,B=-2 (expected -1,-2)
mute+solo canUndo=0
101st action after save isAtSavePoint=1
remove VSTi then undo state=0.9375 (expected 0.1)
add FX/edit/undo/redo state=0.9375 (expected 0.25)
Cmd-Z in text input: [preventDefault, undo]
Cmd-Shift-Z uppercase key: []
```

The FX probe substitutes edited state through the real effect engine rather
than operating a vendor GUI. This isolates the action's missing-state-capture
problem without depending on a particular plugin's editor or Undo features.
