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
  int position = 0;
  bool active = true;
  bool muted = false;
  bool soloed = false;
  float gainDb = 0.0f;
  int pluginUid = 0;
  std::vector<std::uint8_t> pluginState;
  std::string expressionMapId;
};

enum class PatchDeleteResult { deleted, referenced, notFound, error };

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
  PatchDeleteResult deletePatch(const std::string &patchId);

  bool upsertChair(const ChairRow &chair);
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

private:
  sqlite3 *database_;
  std::mutex &databaseMutex_;
};

} // namespace fiddle
