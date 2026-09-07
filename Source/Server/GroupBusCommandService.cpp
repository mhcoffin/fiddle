#include "GroupBusCommandService.h"

#include "GroupBusActions.h"
#include "MixerModel.h"
#include "UndoManager.h"

namespace fiddle {

GroupBusCommandService::GroupBusCommandService(MixerModel &mixer,
                                               UndoManager &undoManager)
    : mixer_(mixer), undoManager_(undoManager) {}

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

bool GroupBusCommandService::setStripDirectOutput(
    const juce::String &stripId, const juce::String &busId) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip || (busId.isNotEmpty() && !mixer_.getGroupBus(busId)))
    return false;
  undoManager_.perform(std::make_unique<SetStripOutputAction>(
      mixer_, stripId, strip->directOutputBusId, busId));
  return true;
}

} // namespace fiddle
