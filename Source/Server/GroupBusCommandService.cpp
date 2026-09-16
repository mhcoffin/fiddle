#include "GroupBusCommandService.h"

#include "GroupBusActions.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoManager.h"

#include <set>

namespace fiddle {

GroupBusCommandService::GroupBusCommandService(MixerModel &mixer,
                                               PluginScanner &scanner,
                                               UndoManager &undoManager)
    : mixer_(mixer), scanner_(scanner), undoManager_(undoManager) {}

bool GroupBusCommandService::addGroupBus(
    const juce::String &name, const std::vector<juce::String> &stripIds) {
  if (name.trim().isEmpty())
    return false;
  undoManager_.perform(
      std::make_unique<AddGroupBusAction>(mixer_, name, stripIds));
  return true;
}

bool GroupBusCommandService::removeGroupBus(const juce::String &busId) {
  if (!mixer_.getGroupBus(busId))
    return false;
  undoManager_.perform(std::make_unique<RemoveGroupBusAction>(mixer_, busId));
  return true;
}

bool GroupBusCommandService::renameGroupBus(const juce::String &busId,
                                            const juce::String &name) {
  auto *bus = mixer_.getGroupBus(busId);
  if (!bus || name.trim().isEmpty())
    return false;
  undoManager_.perform(std::make_unique<RenameGroupBusAction>(
      mixer_, busId, bus->name, name.trim()));
  return true;
}

bool GroupBusCommandService::moveGroupBus(const juce::String &busId,
                                          int newIndex) {
  const int oldIndex = mixer_.groupBusIndex(busId);
  if (oldIndex < 0)
    return false;
  undoManager_.perform(std::make_unique<MoveGroupBusAction>(
      mixer_, busId, oldIndex, newIndex));
  return true;
}

bool GroupBusCommandService::setGroupBusGain(const juce::String &busId,
                                             float gainDb) {
  auto *bus = mixer_.getGroupBus(busId);
  if (!bus)
    return false;
  gainDb = juce::jlimit(-120.0f, 12.0f, gainDb);
  undoManager_.perform(std::make_unique<SetGroupBusGainAction>(
      mixer_, busId, bus->gainDb(), gainDb));
  return true;
}

bool GroupBusCommandService::setGroupBusMute(const juce::String &busId,
                                             bool muted) {
  auto *bus = mixer_.getGroupBus(busId);
  if (!bus)
    return false;
  undoManager_.perform(std::make_unique<SetGroupBusMuteAction>(
      mixer_, busId, bus->isMuted(), muted));
  return true;
}

bool GroupBusCommandService::setGroupBusSolo(const juce::String &busId,
                                             bool soloed) {
  auto *bus = mixer_.getGroupBus(busId);
  if (!bus)
    return false;
  undoManager_.perform(std::make_unique<SetGroupBusSoloAction>(
      mixer_, busId, bus->isSoloed(), soloed));
  return true;
}

bool GroupBusCommandService::addGroupBusInsert(
    const juce::String &busId, int pluginUid, StripInsertPosition position) {
  if (!mixer_.getGroupBus(busId))
    return false;
  for (const auto &description : scanner_.getKnownPluginList().getTypes()) {
    if (description.uniqueId != pluginUid)
      continue;
    if (!PluginCompatibility::fromDescription(description,
                                               PluginSlotRole::effect)
             .compatible)
      return false;
    undoManager_.perform(std::make_unique<AddGroupBusInsertAction>(
        mixer_, busId, description, position));
    return true;
  }
  return false;
}

bool GroupBusCommandService::removeGroupBusInsert(
    const juce::String &busId, const juce::String &slotId) {
  auto *bus = mixer_.getGroupBus(busId);
  if (!bus || !bus->audioEngine().positionOf(slotId))
    return false;
  undoManager_.perform(
      std::make_unique<RemoveGroupBusInsertAction>(mixer_, busId, slotId));
  return true;
}

bool GroupBusCommandService::moveGroupBusInsert(
    const juce::String &busId, const juce::String &slotId,
    StripInsertPosition position, int newIndex) {
  auto *bus = mixer_.getGroupBus(busId);
  if (!bus || !bus->audioEngine().positionOf(slotId) || newIndex < 0 ||
      newIndex > bus->audioEngine().insertCount(position))
    return false;
  undoManager_.perform(std::make_unique<MoveGroupBusInsertAction>(
      mixer_, busId, slotId, position, newIndex));
  return true;
}

bool GroupBusCommandService::setGroupBusInsertBypassed(
    const juce::String &busId, const juce::String &slotId, bool bypassed) {
  auto *bus = mixer_.getGroupBus(busId);
  const auto snapshot = bus ? bus->audioEngine().snapshot(slotId)
                            : std::optional<AudioInsertSnapshot>{};
  if (!snapshot)
    return false;
  undoManager_.perform(std::make_unique<BypassGroupBusInsertAction>(
      mixer_, busId, slotId, snapshot->bypassed, bypassed));
  return true;
}

bool GroupBusCommandService::toggleGroupBusInsertEditor(
    const juce::String &busId, const juce::String &slotId) {
  auto *bus = mixer_.getGroupBus(busId);
  return bus && bus->audioEngine().toggleEditor(slotId, bus->name + " bus");
}

bool GroupBusCommandService::setStripDirectOutput(
    const juce::String &stripId, const juce::String &busId) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip || (busId.isNotEmpty() && !mixer_.getGroupBus(busId)))
    return false;
  undoManager_.perform(std::make_unique<SetStripOutputAction>(
      mixer_, stripId, strip->directOutputBusId, busId));
  return true;
}

bool GroupBusCommandService::setGroupStripDirectOutput(
    const std::vector<juce::String> &stripIds, const juce::String &busId) {
  if (stripIds.empty() ||
      (busId.isNotEmpty() && !mixer_.getGroupBus(busId)))
    return false;

  std::set<juce::String> uniqueIds;
  std::vector<SetGroupStripOutputAction::PreviousOutput> previousOutputs;
  previousOutputs.reserve(stripIds.size());
  for (const auto &stripId : stripIds) {
    auto *strip = mixer_.getStrip(stripId);
    if (!strip || !uniqueIds.insert(stripId).second)
      return false;
    if (strip->directOutputBusId != busId)
      previousOutputs.push_back({stripId, strip->directOutputBusId});
  }

  if (previousOutputs.empty())
    return false;
  return undoManager_.perform(std::make_unique<SetGroupStripOutputAction>(
      mixer_, std::move(previousOutputs), busId));
}

} // namespace fiddle
