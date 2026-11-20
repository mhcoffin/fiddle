# Expression and percussion maps: one shared collection

Status: **implemented** (2026-11-18). This replaces the per-library design
of 2026-11-04 (in git history as `docs/library-map-import.md`). A rehearsal on
a copy of the live database turned its 164 per-library rows into 140 maps (129
expression, 11 percussion) with no clashes and no missing references.

## Why the change

The 2026-11-04 design gave each library its own copies of its maps and limited
every picker to the layer's or patch's library. That fixed two problems: the
375-map picker, and patches breaking when a file in Dorico's folder changed.
Scoping by library was a mistake, though. A map belongs to a plug-in patch,
and a library holds many patches. A violin layer in Duality Strings was offered
the library's 7 maps, of which one was a violin map. Other maps that would work
on that violin layer (VSL's own maps, other Art Conductor variants, custom maps)
were never offered. A library with 90 maps also overloaded the drop-down.

This design keeps what worked and drops the scoping:

- **Kept:** maps are copies in Fiddle's database, not links to Dorico's files,
  with the source tracking and update checks.
- **Dropped:** libraries owning maps. There is one collection of maps,
  and any patch or layer can use any of them.
- **New:** a picker with search, suggestions based on how maps are already used,
  and favorites.

## Where it lives

| Piece | Code |
| --- | --- |
| Storage | `maps` table, `LibraryMapRow`, `LibraryRoutingRepository` (`listMaps`, `getMap`, `upsertMap`, `insertMapIfAbsent`, `setMapFavorite`, `listMapUsage`, `mapReferences`, `deleteMap`, `missingMapReferences`) |
| Reading, fingerprints, migration | `LibraryMaps.{h,cpp}` (`readLibraryMapFile`, `libraryMapHash`, `compileExpressionMap`, `doricoMapSources`, `migrateLibraryMaps`) |
| Undo | `LibraryMapCatalogAction` (`LibraryCatalogAction.h`), on the catalog undo stack |
| Server | `LibraryMapUi.cpp`: lookups for playback, IPC, update checks |
| UI | `MapPicker.svelte` (picker), `MapImportDialog.svelte` (import list), `LibraryMaps.svelte` (the Maps manager panes), `libraryMaps.js` (ranking, search) |
| Tests | `LibraryMapsTest.cpp`, `LibraryRoutingRepositoryTest.cpp`, `ui/tests/libraryMaps.test.js`, `browser-tests/chairLayout.mjs` |

## Storage

| Table | Key | Columns |
| --- | --- | --- |
| `maps` (replaces `library_maps`) | `(kind, entity_id)`; `kind` is `expression` or `percussion` | `name`, `xml` (the map's own definition element), `revision`, `source_path`, `source_version`, `source_hash`, `edited`, `favorite`, `updated_at` |

The source columns are unchanged:

- `source_path`: the `.doricolib` file the map was imported from; empty for
  maps made in Fiddle.
- `source_version`: the map's own version in that file at import time.
- `source_hash`: a SHA-256 fingerprint of the normalized definition.
- `edited`: the map was edited in Fiddle after import (percussion only).

`favorite` is a preference rather than catalog content. Toggling it is
immediate, is not undoable and doesn't change `revision`. Saving or undoing a
map keeps its star.

- `library_patches.expression_map_id` / `percussion_map_id` and
  `layers.expression_map_id` / `percussion_map_id` refer to `maps` by kind
  and entity ID. A library no longer matters for lookups.
- **Project versions are unchanged.** `strip_blobs` still holds each layer's
  copy of its map, so old versions restore exactly as before.
- **Library snapshots no longer carry maps.** Catalog undo of a library edit,
  and deleting a library, leave the maps alone.

### Migration (on first launch)

In one transaction in `ensureSchema`:

1. Create `maps`.
2. Copy `library_maps` into it, one row per `(kind, entity_id)`. When libraries
   hold different copies of the same ID, the copy updated most recently wins
   (ties: higher revision, then library ID). The others are listed on stderr.
   The live database has no such clashes: 153 expression rows hold 129 maps,
   and the 24 IDs in more than one library are identical in every copy.
3. Drop `library_maps`.

`migrateLibraryMaps` (MainComponent startup) still fills in maps that are
referenced but missing, from Dorico's folders or a saved project version. It
now works per map ID rather than per library and map ID.

## Importing

The import tools move from the library editor to the **Maps manager**. Its two
panes, **Expression Maps** and **Percussion Maps**, open from buttons on
the Library Manager's home page.

- **Import .doricolib…** and **Find in Dorico Library…** list the maps found,
  as before, with a search box. Find in Dorico Library starts with an empty search.
- Statuses compare with Fiddle's own copy: **New**, **Newer version**, **Already
  in Fiddle**, **Differs from Fiddle's copy**. A map that is already in use shows
  **used by N**. Importing a different version of a map replaces it for every
  patch and layer that uses it, so the list says so before you confirm.
- The table of maps gains a search box, a ☆ column and a **Used by** count
  whose tooltip lists the patches and layers. **Export**, **Delete** (blocked
  while in use), **Update** and, for percussion maps, **Edit**, **Duplicate** and
  **+ New Map** work as before.
- **Check for updates** runs when a pane first opens and when you click it,
  over every map with a source.
- The picker has the same import buttons, so you never have to leave Layer
  Details. If one map is imported from the picker, it's also chosen.

Imports, updates and deletes are still undoable catalog edits ("Undo catalog").

Only a change to a map itself reaches the layers: importing, updating or
editing a map (or undoing that) gives the layers using that map the new copy.
Fiddle compares the collection before and after each catalog change
(`syncLayersToChangedMaps`, `changedMaps` in `LibraryMaps.h`), so other catalog
changes (library edits, preset captures, an agent's library edits, and their
Undo) leave every layer's map alone. A restored older version keeps its own
copies of maps until one of those maps is itself changed.

The old **Load from file…** buttons are gone: the picker's import does the
same job and shows the clash warnings. `loadPercussionMapFromFile` is removed.
`loadExpressionMapFromFile` stays on the expression-map command boundary
(`ExpressionMapCommands::importFile`, used by its tests) with no UI. It imports
into Fiddle's maps and refuses to replace a different copy that other patches
or layers use.

## The picker

`MapPicker.svelte` is used for both kinds, in Layer Details and on library
editor patch rows. Closed, it is a button showing the current map, plus
**View**. Clicking the button opens a panel attached to `<body>`, so scrolling
containers don't clip it. The panel has:

- a search box, focused when the panel opens; every word must match the name,
  entity ID, creator, player names or source file;
- filters: **All**, **Favorites**, **This library** (maps used by any patch in
  the patch's library, which was the old list);
- a **None** row, then the maps;
- the **Import .doricolib…** and **Find in Dorico Library…** buttons.

**Ranking.** With no search and the All filter, the list is in three sections:

1. **Suggested.** Ordered as follows:
   - maps used by this patch or by layers made from it, labelled "This patch";
   - then maps used on other patches or layers of the same Dorico instrument,
     labelled with the instrument and the use count. Instrument aliases count
     as the same instrument;
   - for percussion, also maps that cover the chair's instrument.
2. **Favorites**, minus any already suggested.
3. **All maps**: the rest, by name.

With a search, the matches are one list, still ranked suggested first, then
favorites, then by name. The current map is always marked. Percussion maps
that don't cover the instrument keep their "(does not cover this instrument)"
note.

**Keyboard:** ↑/↓ move, Enter chooses, Escape closes the panel without closing
the dialog it's in. Each row's ☆ toggles the favorite without choosing the map.

Suggestions come from usage, which the server sends with each map in
`setLibraryMaps`: `uses: [{type: "patch" | "layer", patchId, libraryId,
instrument, label}]`. A patch's instrument is its classification; a layer's is
its chair's instrument. Usage changes with every layer edit, so it also
arrives on its own in `setLibraryMapUsage`.

## Playback and lookups

Lookups by `(libraryId, entityId)` become lookups by entity ID:
`loadPercussionMap(entityId)` and `loadStoredExpressionMap(entityId)`.
`ExpressionMapSources` has `stored(entityId)` in place of `forStrip` and
`forLibrary`. The viewer (`requestExpressionMapDetails`) looks in Fiddle's
maps first, then in Dorico's library. Restoring a project version still
prefers its saved copy (`loadPersisted`).

Restoring a version saved before chairs (`LegacyStripConversion`) generates
libraries and patches as before. The maps saved with its strips are added with
`insertMapIfAbsent`: a map Fiddle already has is kept as it is.

## IPC

| Message | Payload |
| --- | --- |
| `requestLibraryMaps` → `setLibraryMaps` | each map: `kind`, `entityId`, `name`, `revision`, `version`, `sourcePath`, `sourceVersion`, `edited`, `favorite`, `creator`, `pluginNames`, `references` (labels), `uses`; percussion also `instruments`, `entryCount`, `map` |
| → `setLibraryMapUsage` | `[{kind, entityId, type, patchId, libraryId, instrument, label}]`: usage alone, sent with every layer-status push (after project and catalog edits) and after an expression map assignment; the UI merges it into the maps (`applyMapUsage`) |
| `chooseLibraryMapFiles` | `[kind, requestId]` |
| `findDoricoLibraryMaps` | `[kind, requestId]` |
| → `libraryMapImportCandidates` | `{kind, requestId, origin, maps, errors}`; each candidate also has `references` (how many patches and layers use Fiddle's copy) |
| `importLibraryMaps` | `[kind, [{file, entityId}], requestId]` |
| → `libraryMapResult` | `{kind, action, success, message, entityId, requestId, imported}` |
| `saveLibraryMap` | `[percussion map JSON]` |
| `deleteLibraryMap`, `exportLibraryMap` | `[kind, entityId]` |
| `setLibraryMapFavorite` | `[kind, entityId, favorite]` |
| `checkLibraryMapSources` → `libraryMapSourceStatus` | `[]` → `{statuses}` |
| `updateLibraryMap` | `[kind, entityId, file?]` |
| `requestExpressionMapDetails` | `[entityId]` |

`requestId` lets the panel that asked handle the answer. The Maps manager and
any open picker all listen for the same messages.

## Tests

- **Repository:** migration from `library_maps`, including a clash (the newest
  copy wins) and a rerun; favorites survive upsert and undo; usage rows and
  reference labels across libraries; delete blocked while in use; library
  undo and delete leave maps alone.
- **Server (`LibraryMapsTest`):** playback lookups, the startup migration
  filling missing maps, legacy conversion keeping a stored map.
- **UI unit (`libraryMaps.test.js`):** search, ranking (this patch, same
  instrument and aliases, percussion coverage, favorites, the This library
  filter), import statuses.
- **Browser (`chairLayout.mjs`):** the Maps panes float over the window from
  the editor header and close with Escape; the patch-row picker lists maps
  from other libraries in its sections, searches, stars a map without
  closing, chooses with Enter, and Escape closes only the list.

## Every layer has a library (restoring versions saved before chairs)

Chair layers always come from a library patch (`assignPatchToChair`), so they
have a library. The exception is the **free-standing mixer strip**, left over
from before chairs existed. It has no chair, and its "library" is just an
editable name. The current UI can't create one: `addMixerStrip` and the old
`buildPlaybackTemplate` path are no longer called from anywhere. One can still
appear by restoring an old project version, and **Duplicate strip** in Layer
Details can copy it.

This design removes free-standing strips entirely.

1. **Convert them** (`LegacyStripConversion.h`, called from
   `ProjectRestoreService`). The live database has none: every row in
   `strips` is a chair layer. So conversion happens only when a restored
   project version predates chairs (`routingState.schemaVersion` 0):
   - **Libraries.** Strips with the same library name share a generated
     library named "<name> (restored)", or "Unfiled (restored)" when the name
     is empty. Each library's ID is derived from its name, so restoring
     another old version reuses the library. The user's own libraries of the
     same name are left alone.
   - **Patches.** Each distinct setup (plug-in, plug-in state, expression map
     and percussion map) becomes one patch. The patch is named after its map.
     Maps saved with the strip join Fiddle's maps unless Fiddle already has
     one with that ID. Expression maps an old version didn't save are filled
     in afterwards by the map migration (`migrateLibraryMaps`), from Dorico's
     folders. If a patch is already in
     the stored library, the stored copy wins, so the user's edits survive.
   - **Chairs.** Each MIDI port and channel becomes a chair at the same input
     (`flat_index = port × 16 + channel`).
     - If the current project already has a chair on that input, the new
       chair copies its instrument, name, role and ordinal, so Dorico's
       current template still reaches it.
     - Otherwise the chair is a stand-in for the strip's family (a violin for
       strings, a flute for wind, and so on), named after the family and
       input, for example "Brass 1/4".
     - Strips on input 0 (never a chair input) or with no input get fresh
       inputs after the highest one used.
   - **Layers.** Every strip that holds anything (a plug-in, a map, scripts or
     effects) becomes a layer on its chair. It keeps its gain, mute, solo,
     sends, effects and plug-in state. Empty strips, which old templates made
     for every channel, leave just the chair.

   The restore logs which libraries it made or extended. The Library Manager
   shows them like any other.

   Tests and other callers that construct `ProjectRestoreService` without a
   repository still restore old versions as plain strips. The app always
   passes one.
2. **Remove the leftover code** (done):
   - the `addMixerStrip` handler and `MixerCommandService::addStrip`, with its
     undo action;
   - `buildPlaybackTemplate`, including its `library_instruments` strip setup
     and `FiddleDatabase::loadAllLibraryInstruments`;
   - `duplicateStripInput`, `MixerModel::duplicateStripAfter`, and the
     **Duplicate strip** button;
   - `setStripLibrary`, `setGroupLibrary`, and the editable library name in
     Layer Details;
   - the "free-standing mixer strip" wording.

   Still present: `MixerModel::addStrip` (the integration tests build mixers
   with it), `removeMixerStrip`, and the server's handler for the plug-in's
   old `restore_state` message. That handler rebuilds strips from a state
   blob. The plug-in no longer sends this message; only `fiddle_mock load`
   does.

Then every layer has a library by construction, and Load from file always has
a library to import into.

## Checking sources for updates

Fiddle checks on demand rather than watching Dorico's folders: the first time
a Maps manager pane opens, and again whenever **Check for updates** is
clicked. For each map with a `source_path`:

1. **Reread the source file** and find the map by entity ID. Compare it with
   the stored `source_version` and `source_hash`:
   - **Up to date:** same hash.
   - **Newer version:** the file's `version` is higher.
   - **Differs:** same or lower version, different hash. This happens when a
     vendor edits a map without bumping its version.
2. **If the file is gone, or no longer contains the map,** search Dorico's
   `DefaultLibraryAdditions` folders for the same entity ID. If it's found,
   compare as above and offer to re-point `source_path` to the new file.
   Otherwise mark the map **Source missing**. It keeps playing from the stored
   copy.
3. **Show the status** in the map table. **Update** replaces the stored
   definition with the file's, through the same undoable catalog edit as an
   import, and resets `source_version`, `source_hash` and `edited`. For a
   percussion map marked `edited`, Update first warns that it discards the
   edits made in Fiddle.

Maps with no `source_path` (created in Fiddle, or missing at migration) are
not checked.
