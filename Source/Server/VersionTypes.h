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

/// A strip blob contains every setting for a mixer strip, including a UUID
/// to ensure distinct strips always produce distinct hashes even if their
/// settings are otherwise identical.
struct StripBlob {
  std::string uuid; ///< Embedded UUID — ensures unique hashes.

  // Strip settings (serialized as JSON for readability/debuggability)
  std::string library;
  std::string family;
  bool isSolo = true;
  int inputPort = -1;
  int inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityId;

  /// Binary VST3 plugin state. Stored separately in the blob for efficiency.
  std::vector<uint8_t> pluginState;

  /// Serialize all fields into a canonical byte sequence for hashing.
  /// The format is not meant to be human-readable — it's just for hashing.
  std::string serializeForHash() const {
    std::ostringstream os;
    os << uuid << '\0' << library << '\0' << family << '\0'
       << (isSolo ? '1' : '0') << '\0' << inputPort << '\0' << inputChannel
       << '\0' << pluginUid << '\0';
    // Use fixed-precision for float to ensure deterministic hashing
    os << std::fixed << std::setprecision(6) << gainDb << '\0';
    os << expressionMapEntityId << '\0';
    std::string header = os.str();
    // Append binary plugin state
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

struct GlobalState {
  float masterGainDb = 0.0f;
  // Add more global fields here as needed.

  std::string serializeForHash() const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(6) << masterGainDb;
    return os.str();
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
  std::vector<Hash> stripHashes; ///< Ordered left-to-right.

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
  Hash stateHash;       ///< Points to a FiddleState.
  BranchId branchId;    ///< The branch this version belongs to.
  Hash parentHash;      ///< Parent version hash (empty for root).
  Hash mergeParentHash; ///< Merge-parent hash (empty if not a merge).

  Hash computeHash() const {
    std::string combined =
        stateHash + ":" + branchId + ":" + parentHash + ":" + mergeParentHash;
    return xxh3_128(combined);
  }
};

} // namespace fiddle::versioning
