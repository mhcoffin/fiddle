#pragma once

#include <cstdint>
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
  int revision = 1;
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
  std::optional<LibraryPatchRow> getPatch(const std::string &patchId) const;
  std::vector<LibraryPatchRow>
  listPatches(const std::string &libraryId = {}) const;
  int patchUsageCount(const std::string &patchId) const;
  /// Count linked layers whose library-owned setup no longer matches the
  /// current catalog patch. Layer-owned mixer controls are ignored.
  int outOfDateLayerCount(const std::string &patchId) const;
  /// Return the IDs of all layers whose library-owned setup differs from
  /// their current source patch. Layers whose source patch is missing are not
  /// included; callers can report that condition separately.
  std::vector<std::string> outOfDateLayerIds() const;
  /// Copy the current catalog patch defaults to every linked layer while
  /// preserving layer-owned routing and mixer controls. Returns the number of
  /// updated layers, or std::nullopt on failure/missing patch.
  std::optional<int>
  updateLayersFromPatch(const std::string &patchId);
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
  sqlite3 *database_;
  std::mutex &databaseMutex_;
};

} // namespace fiddle
