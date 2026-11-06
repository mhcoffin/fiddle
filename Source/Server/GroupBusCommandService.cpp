#include "GroupBusCommandService.h"

#include "GroupBusActions.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoManager.h"

#include <set>

namespace fiddle {

// Layer send commands edit the layer's chair bus: every layer of the chair
// shares its sends (docs/chair-buses.md).
namespace {
template <typename Edit>
bool editChairSends(MixerModel &mixer, UndoManager &undo, const juce::String &stripId,
                    Edit &&edit, juce::String gesture = {}) {
  auto *chair = mixer.chairBusForStrip(stripId);
  if (!chair) return false;
  auto sends = chair->sends;
  if (!edit(sends, gesture)) return false;
  return undo.perform(std::make_unique<SetChairBusSendsAction>(
      mixer, chair->id, chair->sends, std::move(sends), gesture));
}
} // namespace

bool GroupBusCommandService::addStripSend(const juce::String &stripId, const juce::String &busId) {
  if (!mixer_.isAudioBus(busId)) return false;
  return editChairSends(mixer_, undoManager_, stripId, [&](auto &sends, auto &) {
    if (std::any_of(sends.begin(), sends.end(), [&](const auto &s) { return s.busId == busId; })) return false;
    sends.push_back({juce::Uuid().toString(), busId, -12.0f, true});
    return true;
  });
}

bool GroupBusCommandService::updateStripSend(const juce::String &stripId, const juce::String &sendId,
                                            const juce::String &busId, float gainDb, bool enabled) {
  if (!mixer_.isAudioBus(busId) || !std::isfinite(gainDb) || gainDb < -120 || gainDb > 12) return false;
  auto *chair = mixer_.chairBusForStrip(stripId);
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

bool GroupBusCommandService::removeStripSend(const juce::String &stripId, const juce::String &sendId) {
  return editChairSends(mixer_, undoManager_, stripId, [&](auto &sends, auto &) {
    sends.erase(std::remove_if(sends.begin(), sends.end(), [&](const auto &s) { return s.id == sendId; }), sends.end());
    return true;
  });
}

bool GroupBusCommandService::setStripSendVisible(const juce::String &stripId,
                                                const juce::String &sendId, bool visible) {
  return editChairSends(mixer_, undoManager_, stripId, [&](auto &sends, auto &) {
    const auto found = std::find_if(sends.begin(), sends.end(), [&](const auto &s) { return s.id == sendId; });
    if (found == sends.end()) return false;
    found->showOnStrip = visible;
    return true;
  });
}

bool GroupBusCommandService::setStripSendPreFader(const juce::String &stripId,
                                                 const juce::String &sendId, bool preFader) {
  return editChairSends(mixer_, undoManager_, stripId, [&](auto &sends, auto &) {
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
    const juce::String &name, const std::vector<juce::String> &stripIds) {
  if (name.trim().isEmpty())
    return false;
  undoManager_.perform(
      std::make_unique<AddGroupBusAction>(mixer_, name, stripIds));
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
  return bus && bus->audioEngine().toggleEditor(slotId, bus->name + " bus");
}

bool GroupBusCommandService::setStripDirectOutput(
    const juce::String &stripId, const juce::String &busId) {
  auto *chair = mixer_.chairBusForStrip(stripId);
  if (!chair || (busId.isNotEmpty() && !mixer_.isAudioBus(busId)))
    return false;
  undoManager_.perform(std::make_unique<SetChairBusOutputAction>(
      mixer_, chair->id, chair->directOutputBusId, busId));
  return true;
}

bool GroupBusCommandService::setGroupStripDirectOutput(
    const std::vector<juce::String> &stripIds, const juce::String &busId) {
  if (stripIds.empty() ||
      (busId.isNotEmpty() && !mixer_.isAudioBus(busId)))
    return false;

  // Several selected layers may share a chair; each chair changes once.
  std::set<juce::String> uniqueIds, chairIds;
  std::vector<SetGroupStripOutputAction::PreviousOutput> previousOutputs;
  previousOutputs.reserve(stripIds.size());
  for (const auto &stripId : stripIds) {
    auto *chair = mixer_.chairBusForStrip(stripId);
    if (!chair || !uniqueIds.insert(stripId).second)
      return false;
    if (chairIds.insert(chair->id).second && chair->directOutputBusId != busId)
      previousOutputs.push_back({chair->id, chair->directOutputBusId});
  }

  if (previousOutputs.empty())
    return false;
  return undoManager_.perform(std::make_unique<SetGroupStripOutputAction>(
      mixer_, std::move(previousOutputs), busId));
}

} // namespace fiddle
