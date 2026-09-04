#include "PluginCommandService.h"

#include "FiddleDatabase.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoActions.h"
#include "UndoManager.h"

#include <iostream>
#include <memory>
#include <utility>

namespace fiddle {

PluginCommandService::PluginCommandService(MixerModel &mixer,
                                           PluginScanner &scanner,
                                           UndoManager &undoManager,
                                           FiddleDatabase &database)
    : mixer_(mixer), scanner_(scanner), undoManager_(undoManager),
      database_(database) {}

PluginCatalogState PluginCommandService::catalogState() const {
  PluginCatalogState state;
  state.scanning = scanner_.isScanning();
  state.count = scanner_.getPluginCount();
  state.plugins = juce::JSON::fromString(scanner_.getPluginListAsJson());
  return state;
}

bool PluginCommandService::isPluginAvailable(int pluginUid) const {
  if (pluginUid == 0)
    return true;

  for (const auto &description : scanner_.getKnownPluginList().getTypes()) {
    if (description.uniqueId == pluginUid)
      return true;
  }
  return false;
}

bool PluginCommandService::beginScan(bool fullRescan,
                                     ScanCompletion completion) {
  if (scanner_.isScanning())
    return false;

  auto onComplete = [this, completion = std::move(completion)] {
    if (completion)
      completion(catalogState());
  };
  if (fullRescan)
    scanner_.rescanAsync(database_, std::move(onComplete));
  else
    scanner_.scanIncrementalAsync(database_, std::move(onComplete));
  return true;
}

bool PluginCommandService::setPlugin(const juce::String &stripId,
                                     int pluginUid,
                                     LoadCompletion completion) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr || !isPluginAvailable(pluginUid))
    return false;

  undoManager_.perform(std::make_unique<SetPluginAction>(
      mixer_, scanner_, stripId, strip->pluginUid, pluginUid,
      std::move(completion)));
  return true;
}

bool PluginCommandService::setGroupPlugin(
    const std::vector<juce::String> &stripIds, int pluginUid,
    LoadCompletion completion) {
  if (!isPluginAvailable(pluginUid))
    return false;

  std::vector<std::unique_ptr<UndoableAction>> actions;
  for (const auto &stripId : stripIds) {
    if (auto *strip = mixer_.getStrip(stripId)) {
      actions.push_back(std::make_unique<SetPluginAction>(
          mixer_, scanner_, stripId, strip->pluginUid, pluginUid, completion));
    }
  }

  if (actions.empty())
    return false;

  undoManager_.perform(std::make_unique<CompoundAction>(
      "Group set plugin", std::move(actions)));
  return true;
}

bool PluginCommandService::showEditor(const juce::String &stripId) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr)
    return false;

  strip->showEditor();
  return true;
}

bool PluginCommandService::restoreLibraryState(
    const juce::String &stripId, const juce::String &libraryId,
    const juce::String &entityId, int instanceNumber) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr || !strip->hasPlugin()) {
    std::cerr << "[restoreLibraryPluginState] No strip/plugin for " << stripId
              << std::endl;
    return false;
  }

  const auto instruments = database_.loadLibraryInstruments(libraryId);
  for (const auto &instrument : instruments) {
    if (instrument.entityId != entityId ||
        !instrument.hasInstance(instanceNumber))
      continue;

    if (instrument.pluginState.isEmpty()) {
      std::cerr << "[restoreLibraryPluginState] No blob stored for "
                << entityId << " #" << instanceNumber << std::endl;
      return false;
    }

    const auto restored = strip->applyPluginState(
        instrument.pluginState.getData(),
        static_cast<int>(instrument.pluginState.getSize()));
    if (restored) {
      std::cerr << "[restoreLibraryPluginState] Restored "
                << instrument.pluginState.getSize() << " bytes for strip "
                << stripId << std::endl;
    }
    return restored;
  }

  std::cerr << "[restoreLibraryPluginState] Entity+instance not found in "
               "library"
            << std::endl;
  return false;
}

bool PluginCommandService::restoreLibraryPatchState(
    const juce::String &stripId, const juce::String &patchId) {
  auto *strip = mixer_.getStrip(stripId);
  auto *repository = database_.getLibraryRoutingRepository();
  if (strip == nullptr || !strip->hasPlugin() || repository == nullptr)
    return false;

  const auto patch = repository->getPatch(patchId.toStdString());
  if (!patch || patch->pluginState.empty())
    return false;

  return strip->applyPluginState(
      patch->pluginState.data(), static_cast<int>(patch->pluginState.size()));
}

} // namespace fiddle
