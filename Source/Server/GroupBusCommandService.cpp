#include "GroupBusCommandService.h"

#include "GroupBusActions.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoManager.h"

#include <set>

namespace fiddle {

// Sends and output belong to the chair bus; every layer of the chair shares
// them (docs/chair-buses.md). Commands take a chair bus ID or a layer ID.
namespace {
template <typename Edit>
bool editChairSends(MixerModel &mixer, UndoManager &undo, const juce::String &chairId,
                    Edit &&edit, juce::String gesture = {}) {
  auto *chair = mixer.chairBusFor(chairId);
  if (!chair) return false;
  auto sends = chair->sends;
  if (!edit(sends, gesture)) return false;
  return undo.perform(std::make_unique<SetChairBusSendsAction>(
      mixer, chair->id, chair->sends, std::move(sends), gesture));
}
} // namespace

bool GroupBusCommandService::addChairSend(const juce::String &chairId, const juce::String &busId) {
  if (!mixer_.isAudioBus(busId)) return false;
  return editChairSends(mixer_, undoManager_, chairId, [&](auto &sends, auto &) {
    if (std::any_of(sends.begin(), sends.end(), [&](const auto &s) { return s.busId == busId; })) return false;
    sends.push_back({juce::Uuid().toString(), busId, -12.0f, true});
    return true;
  });
}

bool GroupBusCommandService::updateChairSend(const juce::String &chairId, const juce::String &sendId,
                                            const juce::String &busId, float gainDb, bool enabled) {
  if (!mixer_.isAudioBus(busId) || !std::isfinite(gainDb) || gainDb < -120 || gainDb > 12) return false;
  auto *chair = mixer_.chairBusFor(chairId);
  if (!chair) return false;
  auto sends = chair->sends;
  const auto found = std::find_if(sends.begin(), sends.end(), [&](const auto &s) { return s.id == sendId; });
  if (found == sends.end()) return false;
  const auto gesture = found->busId == busId && found->enabled == enabled ? sendId : juce::String{};
  found->busId = busId;
  found->gainDb = gainDb;
  found->enabled = enabled;
  return undoManager_.perform(std::make_unique<SetChairBusSendsAction>(
      mixer_, chair->id, chair->sends, std::move(sends), gesture));
}

bool GroupBusCommandService::removeChairSend(const juce::String &chairId, const juce::String &sendId) {
  return editChairSends(mixer_, undoManager_, chairId, [&](auto &sends, auto &) {
    sends.erase(std::remove_if(sends.begin(), sends.end(), [&](const auto &s) { return s.id == sendId; }), sends.end());
    return true;
  });
}

bool GroupBusCommandService::setChairSendVisible(const juce::String &chairId,
                                                const juce::String &sendId, bool visible) {
  return editChairSends(mixer_, undoManager_, chairId, [&](auto &sends, auto &) {
    const auto found = std::find_if(sends.begin(), sends.end(), [&](const auto &s) { return s.id == sendId; });
    if (found == sends.end()) return false;
    found->showOnStrip = visible;
    return true;
  });
}

bool GroupBusCommandService::setChairSendPreFader(const juce::String &chairId,
                                                 const juce::String &sendId, bool preFader) {
  return editChairSends(mixer_, undoManager_, chairId, [&](auto &sends, auto &) {
    const auto found = std::find_if(sends.begin(), sends.end(), [&](const auto &s) { return s.id == sendId; });
    if (found == sends.end()) return false;
    found->preFader = preFader;
    return true;
  });
}

GroupBusCommandService::GroupBusCommandService(MixerModel &mixer,
                                               PluginScanner &scanner,
                                               UndoManager &undoManager)
    : mixer_(mixer), scanner_(scanner), undoManager_(undoManager) {}

bool GroupBusCommandService::addGroupBus(
    const juce::String &name, const std::vector<juce::String> &chairIds) {
  if (name.trim().isEmpty())
    return false;
  undoManager_.perform(
      std::make_unique<AddGroupBusAction>(mixer_, name, chairIds));
  return true;
}

bool GroupBusCommandService::removeGroupBus(const juce::String &busId) {
  if (!mixer_.isAudioBus(busId))
    return false;
  undoManager_.perform(std::make_unique<RemoveGroupBusAction>(mixer_, busId));
  return true;
}

bool GroupBusCommandService::renameGroupBus(const juce::String &busId,
                                            const juce::String &name) {
  auto *bus = mixer_.getGroupBus(busId);
  if (!bus || bus->isChairBus() || name.trim().isEmpty())
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
  gainDb = juce::jlimit(-120.0f, bus->maximumGainDb(), gainDb);
  undoManager_.perform(std::make_unique<SetGroupBusGainAction>(
      mixer_, busId, bus->gainDb(), gainDb));
  return true;
}

bool GroupBusCommandService::setStripBalance(const juce::String &id, float balance) {
  auto *strip = mixer_.getStrip(id);
  if (!strip || !std::isfinite(balance) || balance < -1 || balance > 1 ||
      strip->audioEngine().balance() == balance) return false;
  return undoManager_.perform(std::make_unique<SetBalanceAction>(
      mixer_, id, false, strip->audioEngine().balance(), balance));
}

bool GroupBusCommandService::setGroupBusBalance(const juce::String &id, float balance) {
  auto *bus = mixer_.getGroupBus(id);
  if (!bus || !std::isfinite(balance) || balance < -1 || balance > 1 ||
      bus->audioEngine().balance() == balance) return false;
  return undoManager_.perform(std::make_unique<SetBalanceAction>(
      mixer_, id, true, bus->audioEngine().balance(), balance));
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
  if (!bus)
    return false;
  // A chair bus has no name of its own; its window is named after the chair.
  auto title = bus->name + " bus";
  if (bus->isChairBus()) {
    const auto name = chairName ? chairName(bus->chairKey) : juce::String{};
    title = (name.isNotEmpty() ? name : juce::String("Chair")) + " chair";
  }
  return bus->audioEngine().toggleEditor(slotId, title);
}

bool GroupBusCommandService::setChairOutput(
    const juce::String &chairId, const juce::String &busId) {
  auto *chair = mixer_.chairBusFor(chairId);
  if (!chair || (busId.isNotEmpty() && !mixer_.isAudioBus(busId)) ||
      chair->directOutputBusId == busId)
    return false;
  undoManager_.perform(std::make_unique<SetChairBusOutputAction>(
      mixer_, chair->id, chair->directOutputBusId, busId));
  return true;
}

bool GroupBusCommandService::setChairsOutput(
    const std::vector<juce::String> &chairIds, const juce::String &busId) {
  if (chairIds.empty() ||
      (busId.isNotEmpty() && !mixer_.isAudioBus(busId)))
    return false;

  // Several selected layers may share a chair; each chair changes once.
  std::set<juce::String> uniqueIds, chairBusIds;
  std::vector<SetGroupStripOutputAction::PreviousOutput> previousOutputs;
  previousOutputs.reserve(chairIds.size());
  for (const auto &id : chairIds) {
    auto *chair = mixer_.chairBusFor(id);
    if (!chair || !uniqueIds.insert(id).second)
      return false;
    if (chairBusIds.insert(chair->id).second && chair->directOutputBusId != busId)
      previousOutputs.push_back({chair->id, chair->directOutputBusId});
  }

  if (previousOutputs.empty())
    return false;
  return undoManager_.perform(std::make_unique<SetGroupStripOutputAction>(
      mixer_, std::move(previousOutputs), busId));
}

} // namespace fiddle
