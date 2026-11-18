#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace fiddle {

enum class DoricoRole { solo = 0, section = 1 };

struct LibraryPatchRow {
  std::string id;
  std::string libraryId;
  int position = 0;
  std::string name;
  std::string instrumentEntityId;
  std::string family;
  std::string character;
  int pluginUid = 0;
  std::vector<std::uint8_t> pluginState;
  std::string expressionMapId;
  /// This library's percussion map (library_maps.entity_id), or empty.
  std::string percussionMapId;
  /// Play as defaults: chair instrument -> the percussion map instrument a
  /// layer from this patch plays on such a chair (docs/library-percussion-maps.md).
  std::map<std::string, std::string> percussionPlayAs;
  /// Human-readable preset the guided setup asks the user to load.
  std::string expectedPresetName;
  /// False while a guided-import patch still needs explicit user capture.
  bool setupComplete = true;
  int revision = 1;
};

/// A patch's Play as defaults as stored: a flat JSON object, sorted, or
/// empty for none. Decoding ignores anything malformed.
std::string encodePercussionPlayAs(const std::map<std::string, std::string> &playAs);
std::map<std::string, std::string> decodePercussionPlayAs(const std::string &text);

enum class LibraryMapKind { expression, percussion };
const char *libraryMapKindName(LibraryMapKind kind);
std::optional<LibraryMapKind> libraryMapKindFromName(const std::string &name);

// One map in Fiddle's shared collection (docs/maps.md). xml is the map's own
// definition element: ExpressionMapDefinition or DrumKitNoteMapDefinition.
struct LibraryMapRow {
  LibraryMapKind kind = LibraryMapKind::expression;
  std::string entityId, name, xml;
  int revision = 1;
  /// Where the map was imported from; empty for maps made in Fiddle.
  std::string sourcePath;
  /// The map's own version in that file at import time.
  int sourceVersion = 0;
  /// libraryMapHash() of the definition as imported.
  std::string sourceHash;
  /// Edited in Fiddle since import (percussion maps only).
  bool edited = false;
  /// Starred in the picker. A preference: upserts keep the stored value, and
  /// only setMapFavorite() or inserting a new row writes it.
  bool favorite = false;
};

/// One patch or layer that uses a map, for the picker's suggestions.
struct MapUse {
  LibraryMapKind kind = LibraryMapKind::expression;
  std::string entityId;
  bool layer = false;
  std::string patchId, libraryId;
  /// The patch's classification, or the layer's chair instrument.
  std::string instrument;
  /// "Library / Patch" or "Layer Chair / Patch".
  std::string label;
};

// A complete modern catalog entry; absent headers represent deletion. Layer
// rows belong to the project and are deliberately never included. Maps are
// shared by every library and are not part of a library's snapshot.
struct LibraryCatalogSnapshot {
  std::string id, name, vendor, variant;
  bool exists = false;
  std::vector<LibraryPatchRow> patches;
};

struct ChairRow {
  std::string id;
  std::string instrumentEntityId;
  std::string name;
  std::string family;
  DoricoRole role = DoricoRole::solo;
  int ordinal = 1;
  int displayOrder = 0;
  int flatIndex = -1;
  /// Chairs created together as a percussion kit share a kit ID and name
  /// (docs/percussion-kits.md). Empty for an ordinary chair. A kit is only a
  /// grouping: each chair is still one Dorico instrument on one channel.
  std::string kitId;
  std::string kitName;
};

struct LayerRow {
  std::string id;
  std::string chairId;
  std::string patchId;
  /// Immutable display fallback copied when the layer is created. These
  /// fields keep historical layers intelligible if their catalog patch is
  /// later removed.
  std::string patchName;
  std::string libraryId;
  std::string libraryName;
  int position = 0;
  bool active = true;
  bool muted = false;
  bool soloed = false;
  float gainDb = 0.0f;
  int pluginUid = 0;
  std::vector<std::uint8_t> pluginState;
  std::string expressionMapId;
  /// Catalog patch revision last copied into this layer. Unlike serialized
  /// VST state bytes, this remains stable when a plug-in rewrites an
  /// equivalent setup during getStateInformation().
  int sourcePatchRevision = 0;
  bool pluginStateEdited = false;
  std::string percussionMapId;
  /// The percussion map instrument this layer plays as, or empty for the
  /// chair's own (docs/library-percussion-maps.md, "Play as").
  std::string percussionPlayAs;
};

enum class PatchDeleteResult { deleted, referenced, notFound, error };
enum class PatchReplaceResult { replaced, referenced, error };

/// SQLite persistence for the library-patch, Dorico-chair, and layer model.
///
/// The repository owns neither the SQLite handle nor the mutex. FiddleDatabase
/// supplies both so this model participates in the server's existing database
/// serialization without adding another connection.
class LibraryRoutingRepository {
public:
  LibraryRoutingRepository(sqlite3 *database, std::mutex &databaseMutex);

  /// Create the model's tables and indexes. The caller must serialize schema
  /// initialization with all other database access.
  static bool ensureSchema(sqlite3 *database);

  bool upsertPatch(const LibraryPatchRow &patch);
  std::optional<LibraryCatalogSnapshot> captureLibrary(const std::string &id) const;
  PatchReplaceResult restoreLibrary(const LibraryCatalogSnapshot &snapshot,
                                    bool restoreRevisions = true);
  std::optional<LibraryPatchRow> getPatch(const std::string &patchId) const;
  std::vector<LibraryPatchRow>
  listPatches(const std::string &libraryId = {}) const;
  // Catalog auditing must not copy potentially large plug-in state blobs.
  struct ExpressionMapReference { std::string mapId, label; };
  std::vector<ExpressionMapReference> listExpressionMapReferences() const;
  int patchUsageCount(const std::string &patchId) const;

  /// Fiddle's maps, optionally of one kind, by name.
  std::vector<LibraryMapRow>
  listMaps(std::optional<LibraryMapKind> kind = std::nullopt) const;
  std::optional<LibraryMapRow> getMap(LibraryMapKind kind, const std::string &entityId) const;
  /// Insert or replace; the revision advances when the name or XML changes.
  /// An existing row keeps its favorite.
  bool upsertMap(const LibraryMapRow &map);
  /// Insert only when no map of that kind and ID exists. True when the row
  /// was inserted.
  bool insertMapIfAbsent(const LibraryMapRow &map);
  bool setMapFavorite(LibraryMapKind kind, const std::string &entityId, bool favorite);
  /// Every patch and layer that uses a map, in patch then layer order.
  std::vector<MapUse> listMapUsage() const;
  /// The patches ("Library / Patch") and layers ("Layer Chair / Patch")
  /// that use the map.
  std::vector<std::string> mapReferences(LibraryMapKind kind, const std::string &entityId) const;
  /// Refused while referenced.
  PatchDeleteResult deleteMap(LibraryMapKind kind, const std::string &entityId);
  /// Map IDs used by patches or layers that have no map row.
  struct MissingMapReference { LibraryMapKind kind; std::string entityId; };
  std::vector<MissingMapReference> missingMapReferences() const;
  /// Count linked layers whose library-owned setup no longer matches the
  /// current catalog patch. Layer-owned mixer controls are ignored.
  int outOfDateLayerCount(const std::string &patchId) const;
  /// Return the IDs of all layers whose library-owned setup differs from
  /// their current source patch. Layers whose source patch is missing are not
  /// included; callers can report that condition separately.
  std::vector<std::string> outOfDateLayerIds() const;

  // Atomically replace only library-owned setup fields. Reject missing or
  // reassigned layers; never overwrite routing, position or mixer controls.
  bool updateLayerSetups(const std::vector<LayerRow> &layers);
  PatchDeleteResult deletePatch(const std::string &patchId);
  /// Atomically replace one library's catalog rows. Removing a patch used by
  /// any current layer is rejected without changing the catalog.
  PatchReplaceResult
  replaceLibraryPatches(const std::string &libraryId,
                        const std::vector<LibraryPatchRow> &patches);

  /// Insert a new chair and allocate its ordinal, display order, and stable
  /// MIDI destination. A previously deleted matching destination is reclaimed
  /// before a new flat index is allocated. The assigned values are returned in
  /// chair.
  bool insertChairWithStableAssignment(ChairRow &chair);
  bool upsertChair(const ChairRow &chair);
  /// Change a chair's Dorico player type without changing its stable MIDI
  /// destination. Retains the ordinal when available, otherwise allocates the
  /// next ordinal for the new instrument/player-type combination.
  bool changeChairRole(const std::string &chairId, DoricoRole role);
  std::optional<ChairRow> getChair(const std::string &chairId) const;
  std::vector<ChairRow> listChairs() const;
  bool deleteChairAndLayers(const std::string &chairId);
  /// Undo restoration: insert the exact chair/children and release its MIDI
  /// graveyard entry in one transaction. Existing IDs/destinations are rejected.
  bool restoreChairAndLayers(const ChairRow &chair,
                            const std::vector<LayerRow> &layers);
  /// Update existing chair rows atomically, without touching their layers.
  bool updateChairs(const std::vector<ChairRow> &chairs);

  bool upsertLayer(const LayerRow &layer);
  bool createLayerFromPatch(const std::string &layerId,
                            const std::string &chairId,
                            const std::string &patchId, int position);
  std::optional<LayerRow> getLayer(const std::string &layerId) const;
  std::vector<LayerRow>
  listLayers(const std::string &chairId = {}) const;
  bool deleteLayer(const std::string &layerId);
  /// Record a genuine user edit made inside a layer's hosted player.
  bool markLayerPluginStateEdited(const std::string &layerId);

  /// Atomically replace the project-owned routing topology. Catalog patches
  /// are deliberately not part of this operation; layers may retain a
  /// dangling patchId so historical versions remain restorable.
  bool replaceTopology(const std::vector<ChairRow> &chairs,
                       const std::vector<LayerRow> &layers);

private:
  PatchReplaceResult replaceLibraryPatchesUnlocked(const std::string &libraryId,
      const std::vector<LibraryPatchRow> &patches);
  bool upsertChairUnlocked(const ChairRow &chair);
  bool upsertLayerUnlocked(const LayerRow &layer);
  sqlite3 *database_;
  std::mutex &databaseMutex_;
};

} // namespace fiddle
