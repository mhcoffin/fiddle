#pragma once

// Every bridge-map revision Fiddle has installed in Dorico's library, ready
// to decode. A Dorico project keeps its own copy of the maps it was applied
// with, so after a map update an older project still sends an older
// revision's beacon at playback start; the tracker then decodes that channel
// with the matching revision here instead of muting it
// (docs/orchestral-bridge-maps.md, "Older revisions").

#include "BridgeMapDocument.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fiddle {

struct BridgeRevisionArchive {
  /// The map family: the entity ID without its trailing revision
  /// (xmap.user.Fiddle.OrchestralStrings.r5 -> xmap.user.Fiddle.OrchestralStrings).
  /// Beacons are only compared within a family.
  static std::string familyOf(const std::string &entityId) {
    const auto dot = entityId.rfind('.');
    if (dot == std::string::npos || dot + 2 >= entityId.size() || entityId[dot + 1] != 'r')
      return entityId;
    for (size_t i = dot + 2; i < entityId.size(); ++i)
      if (entityId[i] < '0' || entityId[i] > '9') return entityId;
    return entityId.substr(0, dot);
  }

  /// Add a revision; one with the same identity as an earlier one is ignored.
  void add(std::shared_ptr<const CompiledBridgeMap> map) {
    if (!map || map->schema < 2) return;
    auto &revisions = families[familyOf(map->entityId)];
    for (const auto &known : revisions)
      if (known->entityId == map->entityId && known->revision == map->revision) return;
    revisions.push_back(std::move(map));
  }

  struct Match {
    std::shared_ptr<const CompiledBridgeMap> map; // null when none or ambiguous
    bool ambiguous = false;
  };

  /// The revision of this family that sends this beacon. Two revisions
  /// sharing it is ambiguous, and neither is chosen.
  Match find(const std::string &family, const std::array<int, 2> &beacon) const {
    Match match;
    const auto found = families.find(family);
    if (found == families.end()) return match;
    for (const auto &revision : found->second) {
      if (revision->beacon != beacon) continue;
      if (match.map) return {nullptr, true};
      match.map = revision;
    }
    return match;
  }

  /// Another revision of the candidate's family that sends the same beacon,
  /// or null: publishing the candidate would make Dorico's beacon ambiguous.
  std::shared_ptr<const CompiledBridgeMap> collision(const CompiledBridgeMap &candidate) const {
    const auto found = families.find(familyOf(candidate.entityId));
    if (found == families.end() || candidate.schema < 2) return nullptr;
    for (const auto &revision : found->second)
      if (revision->beacon == candidate.beacon &&
          !(revision->entityId == candidate.entityId && revision->revision == candidate.revision))
        return revision;
    return nullptr;
  }

  std::map<std::string, std::vector<std::shared_ptr<const CompiledBridgeMap>>> families;
};

} // namespace fiddle
