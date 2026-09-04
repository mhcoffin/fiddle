#pragma once

// Define XXH_INLINE_ALL before including xxhash so it works header-only.
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace fiddle::versioning {

/// A content hash: 128-bit XXH3 represented as a 32-char lowercase hex string.
using Hash = std::string;

/// A branch identifier: a UUID string.
using BranchId = std::string;

/// A library identifier: a UUID string (stable UUID, mutable name).
/// The nil UUID "00000000-0000-0000-0000-000000000000" is the default library.
using LibraryId = std::string;

/// A version identifier: a random UUID string (not content-addressed).
using VersionId = std::string;

/// The nil UUID used for the default library.
inline const LibraryId kDefaultLibraryId =
    "00000000-0000-0000-0000-000000000000";

// ---------------------------------------------------------------------------
// Hashing utilities
// ---------------------------------------------------------------------------

/// Compute XXH3-128 of raw bytes, return as 32-char hex string.
inline Hash xxh3_128(const void *data, size_t len) {
  XXH128_hash_t h = XXH3_128bits(data, len);
  char buf[33];
  // high64 first for consistent ordering
  std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                (unsigned long long)h.high64, (unsigned long long)h.low64);
  return std::string(buf, 32);
}

/// Compute XXH3-128 of a string.
inline Hash xxh3_128(const std::string &s) {
  return xxh3_128(s.data(), s.size());
}

// ---------------------------------------------------------------------------
// StripBlob — all settings for a single strip
// ---------------------------------------------------------------------------

/// A strip blob contains every setting for a mixer strip. A strip is now
/// uniquely identified by (inputPort, inputChannel, libraryId) — there is no
/// longer a random UUID for uniqueness; the triple provides stable identity.
struct StripBlob {
  std::string libraryId; ///< Library UUID (default = kDefaultLibraryId)

  // Strip settings (serialized as JSON for readability/debuggability)
  std::string library; ///< Human-readable library label (e.g. "SSP")
  std::string family;
  bool isSolo = true;
  bool active = true;
  bool muted = false;
  bool soloed = false;
  int inputPort = -1;
  int inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityId;

  /// Binary VST3 plugin state. Stored separately in the blob for efficiency.
  std::vector<uint8_t> pluginState;

  /// Ordered Lua plugin filenames (basenames, e.g. "force_staccato.lua").
  std::vector<std::string> luaPluginFileNames;

  /// Serialize all fields into a canonical byte sequence for hashing.
  /// Identity key: (inputPort, inputChannel, libraryId) + content fields.
  std::string serializeForHash() const {
    std::ostringstream os;
    os << inputPort << '\0' << inputChannel << '\0' << libraryId << '\0'
       << library << '\0' << family << '\0' << (isSolo ? '1' : '0') << '\0'
       << (active ? '1' : '0') << '\0' << (muted ? '1' : '0') << '\0'
       << (soloed ? '1' : '0') << '\0' << pluginUid << '\0';
    // Use fixed-precision for float to ensure deterministic hashing
    os << std::fixed << std::setprecision(6) << gainDb << '\0';
    os << expressionMapEntityId << '\0';
    // Lua plugins (ordered)
    for (const auto &name : luaPluginFileNames)
      os << name << '\0';
    os << '\0'; // terminator for lua plugin list
    std::string header = os.str();
    // Append binary plugin state
    if (!pluginState.empty())
      header.append(reinterpret_cast<const char *>(pluginState.data()),
                    pluginState.size());
    return header;
  }

  Hash computeHash() const {
    auto data = serializeForHash();
    return xxh3_128(data);
  }
};

// ---------------------------------------------------------------------------
// GlobalState — settings not associated with any strip
// ---------------------------------------------------------------------------

struct PluginSlotBlob {
  std::string slotId;
  std::string formatName;
  int uniqueId = 0;
  std::string fileOrIdentifier;
  std::string manufacturer;
  std::string name;
  std::string category;
  std::string pluginVersion;
  int numInputChannels = 0;
  int numOutputChannels = 0;
  bool bypassed = false;
  std::vector<uint8_t> pluginState;

  std::string serializeForHash() const {
    std::ostringstream os;
    os << slotId << '\0' << formatName << '\0' << uniqueId << '\0'
       << fileOrIdentifier << '\0' << manufacturer << '\0' << name << '\0'
       << category << '\0' << pluginVersion << '\0' << numInputChannels << '\0'
       << numOutputChannels << '\0' << (bypassed ? '1' : '0') << '\0';
    auto data = os.str();
    if (!pluginState.empty())
      data.append(reinterpret_cast<const char *>(pluginState.data()),
                  pluginState.size());
    return data;
  }
};

struct GlobalState {
  int audioSchemaVersion = 1;
  float masterGainDb = 0.0f;
  std::vector<PluginSlotBlob> masterInserts;

  std::string serializeForHash() const {
    std::ostringstream os;
    os << audioSchemaVersion << '\0' << std::fixed << std::setprecision(6)
       << masterGainDb << '\0';
    auto data = os.str();
    for (const auto &insert : masterInserts) {
      const auto serialized = insert.serializeForHash();
      data.append(std::to_string(serialized.size()));
      data.push_back('\0');
      data.append(serialized);
    }
    return data;
  }

  Hash computeHash() const {
    auto data = serializeForHash();
    return xxh3_128(data);
  }
};

// ---------------------------------------------------------------------------
// FiddleState — global state + ordered list of strip hashes
// ---------------------------------------------------------------------------

struct FiddleState {
  GlobalState globalState;
  std::vector<Hash> stripHashes; ///< Ordered left-to-right by (port, channel).

  Hash computeHash() const {
    // Hash = xxh3_128(globalState.hash + ":" + stripHash[0] + ":" + ...)
    std::string combined = globalState.computeHash();
    for (const auto &sh : stripHashes) {
      combined += ':';
      combined += sh;
    }
    return xxh3_128(combined);
  }
};

// ---------------------------------------------------------------------------
// Version — a node in the version DAG
// ---------------------------------------------------------------------------

struct Version {
  VersionId id;            ///< Random UUID primary key.
  Hash stateHash;          ///< Points to a FiddleState.
  BranchId branchId;       ///< The branch this version belongs to.
  VersionId parentId;      ///< Parent version ID (empty for root).
  VersionId mergeParentId; ///< Merge-parent version ID (empty if not a merge).

  /// Wall-clock creation time (ISO-8601 from SQLite CURRENT_TIMESTAMP).
  /// This is display metadata — it is NOT included in any hash.
  std::string createdAt;
};

} // namespace fiddle::versioning
