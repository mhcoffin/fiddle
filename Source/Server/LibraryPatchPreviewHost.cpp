#include "LibraryPatchPreviewHost.h"
#include <algorithm>

namespace fiddle {

LibraryPatchPreviewHost::Preview::Preview(const std::string &patchId) {
  slot.setId({"library-preview:" + juce::String(patchId)});
}

LibraryPatchPreviewHost::LibraryPatchPreviewHost(
    juce::AudioPluginFormatManager &formatManager)
    : formatManager_(formatManager) {}

LibraryPatchPreviewHost::~LibraryPatchPreviewHost() { closeAll(); }

bool LibraryPatchPreviewHost::open(
    const std::string &patchId, const juce::String &title,
    const juce::PluginDescription &description,
    const juce::MemoryBlock &initialState, OpenCompletion completion) {
  if (patchId.empty() || description.uniqueId == 0)
    return false;

  auto existing = previews_.find(patchId);
  if (existing != previews_.end() &&
      existing->second->pluginUid == description.uniqueId &&
      (existing->second->slot.status() == HostedPluginStatus::loaded ||
       existing->second->slot.status() == HostedPluginStatus::loading)) {
    existing->second->active = true;
    if (existing->second->slot.status() == HostedPluginStatus::loaded) {
      existing->second->slot.showEditor(title);
      if (completion)
        completion(true, {});
    }
    return true;
  }

  if (existing != previews_.end())
    previews_.erase(existing);

  auto preview = std::make_unique<Preview>(patchId);
  preview->pluginUid = description.uniqueId;
  auto *previewPtr = preview.get();
  previews_[patchId] = std::move(preview);
  std::vector<std::uint8_t> initialBytes;
  if (!initialState.isEmpty()) {
    const auto *bytes = static_cast<const std::uint8_t *>(initialState.getData());
    initialBytes.assign(bytes, bytes + initialState.getSize());
  }
  seedState(patchId, description.uniqueId, std::move(initialBytes));

  previewPtr->slot.loadPlugin(
      description, formatManager_, 44100.0, 512,
      [previewPtr, title, initialState,
       completion = std::move(completion)](bool success,
                                           const juce::String &error) mutable {
        if (success) {
          if (!initialState.isEmpty())
            previewPtr->slot.applyState(initialState.getData(),
                                        static_cast<int>(initialState.getSize()));
          // Loading and state restoration establish the clean editor baseline.
          static_cast<void>(previewPtr->slot.consumeChangeNotification());
          static_cast<void>(
              previewPtr->slot.consumeExplicitEditNotification());
          static_cast<void>(
              previewPtr->slot.consumeNonParameterStateChangeNotification());
          if (previewPtr->active) previewPtr->slot.showEditor(title);
        }
        if (completion)
          completion(success, error);
      });
  return true;
}

bool LibraryPatchPreviewHost::captureState(
    const std::string &patchId, int expectedPluginUid,
    std::vector<std::uint8_t> &destination) {
  const auto found = previews_.find(patchId);
  if (found == previews_.end() || found->second->slot.status() != HostedPluginStatus::loaded) {
    const auto seed = seeds_.find(patchId);
    if (seed == seeds_.end() || seed->second.pluginUid != expectedPluginUid) return false;
    destination = seed->second.state;
    return true;
  }
  if (found == previews_.end() ||
      found->second->pluginUid != expectedPluginUid ||
      found->second->slot.status() != HostedPluginStatus::loaded)
    return false;

  found->second->slot.refreshStateCache();
  const auto state = found->second->slot.cachedState();
  destination.clear();
  if (!state.isEmpty()) {
    const auto *bytes = static_cast<const std::uint8_t *>(state.getData());
    destination.assign(bytes, bytes + state.getSize());
  }
  return true;
}

std::vector<std::string>
LibraryPatchPreviewHost::consumeChangedPatchIds() {
  std::vector<std::string> changedPatchIds;
  for (auto &[id, preview] : previews_) {
    const bool parameterChanged = preview->slot.consumeChangeNotification();
    const bool explicitEdit =
        preview->slot.consumeExplicitEditNotification();
    const bool nonParameterChanged =
        preview->slot.consumeNonParameterStateChangeNotification();
    if (preview->active && (parameterChanged || explicitEdit || nonParameterChanged))
      changedPatchIds.push_back(id);
  }
  return changedPatchIds;
}

void LibraryPatchPreviewHost::discard(const std::string &patchId) {
  previews_.erase(patchId);
  seeds_.erase(patchId);
}

void LibraryPatchPreviewHost::seedState(const std::string &id, int pluginUid,
                                      std::vector<std::uint8_t> state) {
  seeds_[id] = {pluginUid, std::move(state)};
}

void LibraryPatchPreviewHost::showOnly(const std::vector<std::string> &ids) {
  for (auto &[id, preview] : previews_) {
    preview->active = std::find(ids.begin(), ids.end(), id) != ids.end();
    if (!preview->active) preview->slot.closeEditor();
  }
}

bool LibraryPatchPreviewHost::isLoading() const {
  for (const auto &[id, preview] : previews_)
    if (preview->active && preview->slot.status() == HostedPluginStatus::loading) return true;
  return false;
}

void LibraryPatchPreviewHost::retainOnly(const std::vector<std::string> &ids) {
  for (auto it = previews_.begin(); it != previews_.end();)
    if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) it = previews_.erase(it);
    else ++it;
  for (auto it = seeds_.begin(); it != seeds_.end();)
    if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) it = seeds_.erase(it);
    else ++it;
}

void LibraryPatchPreviewHost::closeAll() { previews_.clear(); seeds_.clear(); }

} // namespace fiddle
