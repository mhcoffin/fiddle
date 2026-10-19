# Technique catalog and coverage audit

## Current plan versus existing tools (2026-09-24)

The active design is [four orchestral section bridge maps](orchestral-bridge-maps.md).
The **Repair plan** and **Chair coverage** tabs still expose earlier diagnostic
algorithms; they have not been relabeled or removed. Do not use their proposals
as the current install plan. In particular, Chair coverage's candidate-product
count is not an estimate of entries needed with mutex-grouped add-ons. A shared
scan budget can be exhausted before a later chair is evaluated; zero evaluated
does not mean zero requirements. See the [superseded analyzer record](chair-coverage-algorithm.md).

Open **View → Windows → Technique Catalog** (or Technique Catalog under the
mixer toolbar's View menu). This is a read-only first step toward an extensible
Dorico-to-Fiddle expression map. It does not generate/install a map, change a
patch assignment, import maps into the existing picker, or modify vendor files.

The default **Assigned-map audit** view works backward from the current project's
actual layer maps, grouping requirements by chair and testing the loaded decoder
and resolver. The **Vocabulary catalog** tab contains the broader source inventory
described below. See [bridge profiles and audit limits](dorico-bridge-profiles.md)
for result meanings and the planned user-facing configuration workflow.
The **Repair plan** tab now proposes identity repairs and selected missing
encodings, previews gains/regressions, and can explicitly export a review JSON.
It does not yet generate an installable Dorico map or activate a new decoder.

## Sources and scopes

On macOS, default discovery reads Dorico 6's built-in playback definitions and
score library under `/Applications`, its user `DefaultLibraryAdditions`, the
bundled bridge map actually loaded by Fiddle, and source maps referenced by
current project layers and Fiddle library patches. Imported project XML is
audited as a saved snapshot, not silently replaced by the source file on disk.
Snapshot paths in the report are labels; no such files are created.

**Include ~/Art-Conductor reference collection** is opt-in; check it and press
**Refresh catalog**. It scans extracted `.doricolib` files, not ZIP archives,
Kontakt snapshots, or plug-in presets. Reference collection membership does not
imply an installed instrument, an active map, or an installed Dorico definition.
**Add source** accepts an additional library file or folder for this session.
Folders scan `.doricolib` files; individual `.xml` files can also be selected.
Additional sources are reference material, not assumed to be installed.

The default view uses current project assignments, falling back to library
assignments or installed maps when none exist. Other scopes are explicit:

- Current project assignments: source-resolved maps used by current layers.
- Fiddle library assignments: maps referenced by saved library patches.
- Installed Dorico maps: all maps discovered in the additions directory,
  including alternatives not used by Fiddle.
- Reference / additional maps: optionally scanned distribution/import sources.
- All discovered vocabulary: also includes definitions not referenced by a map.

Map searches include the creator and source path. Alternative VSL and Art
Conductor setups retain their own map identities and sources; display names
never merge maps. Even repeated map IDs remain separately inspectable. Usage
is source-specific where Fiddle can resolve that source; identity-only matches
are explicitly labeled. Missing assigned sources appear in scan notes.

## What the report means

The catalog links Dorico's `PlayingTechniqueAppearanceDefinition` (score marking)
to `PlayingTechniqueDefinition` (playback identity), then to expression-map
switch references. Both plain `kScoreLibrary` and wrapped
`kScoreLibraryAndOptions` documents are accepted; technique combinations may
use commas or plus signs. Playback definitions retain names, attribute/direction
type, aliases, fallbacks, parents/inheritance masks and exclusion memberships.
Map-specific exclusion groups remain attached to their own maps.

Identical definitions are stored once with all file provenance. Differing
definitions are reported as variants, not automatically resolved. A different
definition in an optional reference collection is distinguished from differing
local definitions. This is not an implementation of Dorico's library override
precedence. Aliases, inheritance, fallback chains and custom project overrides
are not resolved; unresolved definitions are not proof of absence from Dorico.

**Declared** means the technique appears in an explicit switch with a reserved
CC102–119 action in Fiddle's loaded bridge map. It is not proof of correct
combination selection, note association, cancellation, or transmission by Dorico.
The map already embedded in a Dorico project can also differ from the installed
or bundled map. Those behaviors require the planned Dorico acceptance score.

Refresh takes a new snapshot of assignments and files. No automatic watcher is
installed. A low-priority worker parses files without querying the audio engine,
plug-ins or SQLite. The UI only takes lightweight assignment metadata before
the scan. Closing the window leaves a scan running; **Cancel scan** returns
clearly marked partial results. Hidden windows stop polling. For large
distribution scans, prefer stopped playback to minimize disk/CPU competition.

Malformed/unsupported files are skipped with a source-specific note, not repaired.
Files over 64 MiB and symlink traversal outside chosen directories are excluded.
Large reports normalize shared definitions/groups and paginate the technique
table. Map pickers and per-technique reference/source lists show the first 100;
narrow the map search for a specific alternative.

## Verification and next step

`TechniqueCatalogTest` covers source provenance, alternative identities, exact
assignment matching, identical/differing definitions, wrappers, combination
syntax, missing references, malformed input and cancellation using synthetic XML.
The optional `ui/browser-tests/techniqueCatalog.mjs` exercises scopes, searches,
pagination, read-only window commands and polling suspension. It supports an
opt-in real-data scan without connecting to a running Fiddle instance.

Next work is defined by the [orchestral bridge plan](orchestral-bridge-maps.md#next-work-in-order):
focused cancellation/composition tests, four explicit allocation tables, then
matched map/decoder generation and host validation. These diagnostics can inform
that work; their older repair/enumeration schemes do not define the generator.
