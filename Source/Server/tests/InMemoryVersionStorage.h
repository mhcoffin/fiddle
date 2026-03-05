#pragma once

#include "../VersionStoreInterface.h"

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fiddle::versioning {

/// In-memory implementation of IVersionStorage for unit testing.
/// All data lives in std::unordered_maps — no persistence, no dependencies.
class InMemoryVersionStorage : public IVersionStorage {
public:
  // ── Strip blobs ──────────────────────────────────────────────────────

  void putStripBlob(const Hash &hash, const StripBlob &blob) override {
    stripBlobs_[hash] = blob;
  }

  std::optional<StripBlob> getStripBlob(const Hash &hash) const override {
    auto it = stripBlobs_.find(hash);
    if (it == stripBlobs_.end())
      return std::nullopt;
    return it->second;
  }

  bool hasStripBlob(const Hash &hash) const override {
    return stripBlobs_.count(hash) > 0;
  }

  // ── Fiddle states ────────────────────────────────────────────────────

  void putFiddleState(const Hash &hash, const FiddleState &state) override {
    fiddleStates_[hash] = state;
  }

  std::optional<FiddleState>
  getFiddleState(const Hash &hash) const override {
    auto it = fiddleStates_.find(hash);
    if (it == fiddleStates_.end())
      return std::nullopt;
    return it->second;
  }

  bool hasFiddleState(const Hash &hash) const override {
    return fiddleStates_.count(hash) > 0;
  }

  // ── Versions ─────────────────────────────────────────────────────────

  void putVersion(const Hash &hash, const Version &version) override {
    versions_[hash] = version;
  }

  std::optional<Version> getVersion(const Hash &hash) const override {
    auto it = versions_.find(hash);
    if (it == versions_.end())
      return std::nullopt;
    return it->second;
  }

  bool hasVersion(const Hash &hash) const override {
    return versions_.count(hash) > 0;
  }

  void removeVersion(const Hash &hash) override { versions_.erase(hash); }

  std::vector<Hash> getChildVersionHashes(const Hash &hash) const override {
    std::vector<Hash> children;
    for (const auto &[h, v] : versions_) {
      if (v.parentHash == hash || v.mergeParentHash == hash) {
        children.push_back(h);
      }
    }
    return children;
  }

  // ── Branches ─────────────────────────────────────────────────────────

  void putBranch(const BranchId &id, const std::string &name,
                 const Hash &headHash) override {
    branches_[id] = {name, headHash};
  }

  std::optional<std::pair<std::string, Hash>>
  getBranch(const BranchId &id) const override {
    auto it = branches_.find(id);
    if (it == branches_.end())
      return std::nullopt;
    return it->second;
  }

  void updateBranchHead(const BranchId &id, const Hash &newHead) override {
    auto it = branches_.find(id);
    if (it != branches_.end())
      it->second.second = newHead;
  }

  void deleteBranch(const BranchId &id) override { branches_.erase(id); }

  std::vector<std::tuple<BranchId, std::string, Hash>>
  listBranches() const override {
    std::vector<std::tuple<BranchId, std::string, Hash>> result;
    result.reserve(branches_.size());
    for (const auto &[id, pair] : branches_) {
      result.emplace_back(id, pair.first, pair.second);
    }
    return result;
  }

  bool branchNameExists(const std::string &name) const override {
    return std::any_of(
        branches_.begin(), branches_.end(),
        [&](const auto &kv) { return kv.second.first == name; });
  }

  std::optional<BranchId>
  findBranchByName(const std::string &name) const override {
    for (const auto &[id, pair] : branches_) {
      if (pair.first == name)
        return id;
    }
    return std::nullopt;
  }

private:
  std::unordered_map<Hash, StripBlob> stripBlobs_;
  std::unordered_map<Hash, FiddleState> fiddleStates_;
  std::unordered_map<Hash, Version> versions_;
  std::unordered_map<BranchId, std::pair<std::string, Hash>> branches_;
};

} // namespace fiddle::versioning
