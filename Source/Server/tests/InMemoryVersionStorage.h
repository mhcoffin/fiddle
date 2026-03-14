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

  std::optional<FiddleState> getFiddleState(const Hash &hash) const override {
    auto it = fiddleStates_.find(hash);
    if (it == fiddleStates_.end())
      return std::nullopt;
    return it->second;
  }

  bool hasFiddleState(const Hash &hash) const override {
    return fiddleStates_.count(hash) > 0;
  }

  // ── Versions ─────────────────────────────────────────────────────────

  void putVersion(const VersionId &id, const Version &version) override {
    versions_[id] = version;
  }

  std::optional<Version> getVersion(const VersionId &id) const override {
    auto it = versions_.find(id);
    if (it == versions_.end())
      return std::nullopt;
    return it->second;
  }

  bool hasVersion(const VersionId &id) const override {
    return versions_.count(id) > 0;
  }

  void removeVersion(const VersionId &id) override { versions_.erase(id); }

  void updateVersionParent(const VersionId &id,
                           const VersionId &newParentId) override {
    auto it = versions_.find(id);
    if (it != versions_.end())
      it->second.parentId = newParentId;
  }

  std::vector<VersionId>
  getChildVersionIds(const VersionId &parentId) const override {
    std::vector<VersionId> children;
    for (const auto &[id, v] : versions_) {
      if (v.parentId == parentId || v.mergeParentId == parentId) {
        children.push_back(id);
      }
    }
    return children;
  }

  std::vector<std::pair<VersionId, Version>> listAllVersions() const override {
    std::vector<std::pair<VersionId, Version>> result;
    result.reserve(versions_.size());
    for (const auto &[id, v] : versions_) {
      result.emplace_back(id, v);
    }
    return result;
  }

  // ── Branches ─────────────────────────────────────────────────────────

  void putBranch(const BranchId &id, const std::string &name,
                 const VersionId &headId) override {
    branches_[id] = {name, headId};
  }

  std::optional<std::pair<std::string, VersionId>>
  getBranch(const BranchId &id) const override {
    auto it = branches_.find(id);
    if (it == branches_.end())
      return std::nullopt;
    return it->second;
  }

  void updateBranchHead(const BranchId &id,
                        const VersionId &newHeadId) override {
    auto it = branches_.find(id);
    if (it != branches_.end())
      it->second.second = newHeadId;
  }

  void deleteBranch(const BranchId &id) override { branches_.erase(id); }

  std::vector<std::tuple<BranchId, std::string, VersionId>>
  listBranches() const override {
    std::vector<std::tuple<BranchId, std::string, VersionId>> result;
    result.reserve(branches_.size());
    for (const auto &[id, pair] : branches_) {
      result.emplace_back(id, pair.first, pair.second);
    }
    return result;
  }

  bool branchNameExists(const std::string &name) const override {
    return std::any_of(branches_.begin(), branches_.end(),
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

  // ── Libraries ────────────────────────────────────────────────────────

  void putLibrary(const LibraryId &id, const std::string &name) override {
    libraries_[id] = name;
  }

  std::optional<std::string>
  getLibraryName(const LibraryId &id) const override {
    auto it = libraries_.find(id);
    if (it == libraries_.end())
      return std::nullopt;
    return it->second;
  }

  std::vector<std::pair<LibraryId, std::string>>
  listLibraries() const override {
    std::vector<std::pair<LibraryId, std::string>> result;
    result.reserve(libraries_.size());
    for (const auto &[id, name] : libraries_)
      result.emplace_back(id, name);
    return result;
  }

  bool libraryNameExists(const std::string &name) const override {
    return std::any_of(libraries_.begin(), libraries_.end(),
                       [&](const auto &kv) { return kv.second == name; });
  }

  bool renameLibrary(const LibraryId &id, const std::string &name) override {
    auto it = libraries_.find(id);
    if (it == libraries_.end())
      return false;
    it->second = name;
    return true;
  }

private:
  std::unordered_map<Hash, StripBlob> stripBlobs_;
  std::unordered_map<Hash, FiddleState> fiddleStates_;
  std::unordered_map<VersionId, Version> versions_;
  std::unordered_map<BranchId, std::pair<std::string, VersionId>> branches_;
  std::unordered_map<LibraryId, std::string> libraries_;
};

} // namespace fiddle::versioning
