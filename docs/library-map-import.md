# Per-library expression and percussion maps — design

Status: **proposed** (2026-11-04). Not implemented.

## Problem

Sample-library maps are library-specific. A vendor ships its expression maps
and percussion maps together with the library, and each patch uses one of each.
Fiddle doesn't organize them that way:

- **Percussion maps** sit in one shared list (`library_percussion_maps`), with
  the import panel (`PercussionMaps.svelte`) on the Library Manager's home
  page. Every patch row's drop-down offers every map.
- **Expression maps** aren't imported at all. At startup `ExpressionMapLibrary`
  reads Dorico's `DefaultLibraryAdditions` folders (375 maps on the
  development machine). Patches and layers store only the map's entity ID, and
  `xmapLibrary_.load(id)` reads the file from Dorico's folder. The picker is a
  search dialog over all 375 maps.

As a result the drop-downs are long and mostly irrelevant. Editing or deleting
a file in Dorico's folder also silently changes or breaks Fiddle patches.

## Design

Each library owns the maps it uses. Both kinds of map get the same import UI
inside the library editor, and a patch's drop-downs list only its library's
maps.

### Storage

Maps are copied into Fiddle's database and keyed by library:

| Table | Key | Columns |
| --- | --- | --- |
| `library_expression_maps` (new) | `(library_id, entity_id)` | `name`, `xml` (the `.doricolib` map definition), `revision`, `source` (file path or "Dorico library"), `updated_at` |
| `library_percussion_maps` (changed) | `(library_id, entity_id)`, was `entity_id` | unchanged |

- **Copies, not links.** If two libraries use the same vendor map, each has its
  own copy. Re-importing into one doesn't change the other.
- **Lookups use library plus ID.** `library_patches.library_id` and
  `layers.library_id` already exist, so `expression_map_id` and
  `percussion_map_id` are resolved within that library. No new reference
  columns are needed.
- **Project versions are unchanged.** `strip_blobs.expression_map_source` and
  `percussion_map_source` already snapshot each layer's map, so old versions
  restore exactly as before.
- **Deleting a library** deletes its maps.

### Library editor UI

The library editor gets two sections, **Expression Maps** and **Percussion
Maps**. Their layout is almost identical, generalized from today's
`PercussionMaps.svelte`:

- **Import .doricolib…** opens a file chooser. Every map in the file is listed
  with a checkbox and a status (`New`, `Newer version`, `Already in library`,
  `Differs from library`), as percussion import does today. New and newer maps
  are ticked by default.
- **Find in Dorico Library…** lists maps from Dorico's `DefaultLibraryAdditions`
  folders, with a search box. The search starts out filled with the library's
  vendor (or its name, if it has no vendor) and can be edited. Only matches are
  listed; the user ticks the ones to import.
- **A table of imported maps** shows name, entity ID, and how many patches use
  each map. Its actions are **Export**, **Delete** (blocked while patches use the
  map; the tooltip lists them) and, for maps with a newer version on disk,
  **Update**. Percussion maps also keep **Edit**, **Duplicate** and **+ New Map**.
- Imports, updates and deletes are undoable catalog edits, recorded in the
  library history like other catalog changes ("Undo catalog").

The home page's Percussion Maps panel is removed.

### Pickers

- **Patch rows in the library editor:** the expression-map and percussion-map
  drop-downs list only that library's maps. A short list doesn't need the
  current 375-entry search dialog, so the expression-map picker becomes a
  plain drop-down like the percussion one. The viewer button stays. Percussion
  maps keep their ordering, with maps covering the patch's instrument first.
- **Layer Details in the mixer:** the same per-library lists, from the layer's
  `library_id`. **Load from file…** stays as an escape hatch for both map
  kinds. The chosen map is imported into the layer's library, through the same
  undoable edit and status rules as an import, and then assigned to the layer.
  Its patch row then shows it in the drop-down. (Today the percussion version
  already adds the file to the shared list, and the expression-map version
  keeps the map on the layer only.)

### Playback and lookups

The catalog lookups by ID (`xmapLibrary_.load`) become lookups within a
library:

| Call site | Change |
| --- | --- |
| Patch setup (`MainComponent.cpp:6209`) | Look up the patch's library map |
| Layer sync (`MainComponent.cpp:6299`) | Look up the layer's library map |
| Older library-instrument path (`MainComponent.cpp:5679`) | Use the library's map, or drop the path if nothing still calls it |
| `loadPersisted` (project restore, lines 845, 1906, 2046) | Unchanged: it prefers the project's saved map copy |
| `loadPercussionMap(id)` | Takes the layer's or patch's library |
| `requestExpressionMapDetails` (viewer, Note Inspector) | Resolve within the library |

`ExpressionMapLibrary` remains only as the source for **Find in Dorico
Library…** (and its directory scan at lines 2246 and 2593). The universal
bridge map (`Fiddle_Universal.doricolib`) and the section bridge maps are
Fiddle's own Dorico-side maps and are unaffected.

## Migration (one-time, on first launch)

1. **Expression maps:** for each library, collect the `expression_map_id`
   values used by its patches and layers. Copy each map's definition from
   Dorico's folder (via `ExpressionMapLibrary`) into
   `library_expression_maps`. If a map's file is gone, use the latest project
   version's saved map copy (`expression_map_source`). If neither exists, keep
   the ID, mark the map missing in the library editor (as the pickers' "not in
   library" label does today), and list it in the migration report.
2. **Percussion maps:** copy each map into every library whose patches or
   layers use it. **Drop maps no library uses.**
3. Write a one-line migration summary to the log, giving per-library counts and
   any missing maps.

The migration runs in one transaction and is idempotent: rerunning it finds
nothing new to copy.

## Open questions

- **A layer with no library** (`library_id` empty): "Load from file…" can't
  pick a library. Proposal: keep the map on the layer only, as the
  expression-map file load does today, and say so in the Layer Details status
  line.
- **Updating from disk:** compare `version` and the definition text against
  the source file on demand (when the editor opens, or with a refresh button),
  rather than watching Dorico's folders.

## Tests

- **`LibraryRoutingRepositoryTest`:** scoping by library (the same ID in two
  libraries holds different content), the migration (copied, missing and
  dropped maps, and a rerun that copies nothing), deleting a library, and the
  delete block while a map is in use.
- **Playback:** two libraries with different maps under the same entity ID;
  each layer plays its own library's map.
- **UI unit tests:** the generalized import offer, search filtering and
  default ticks.
- **Browser test (`chairLayout.mjs` or a new `libraryManager.mjs`):** both
  library editor sections, a patch drop-down that lists only that library's
  maps, and Load from file adding the map to the library.
