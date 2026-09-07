#include "StripAudioCommandService.h"

#include "MixerModel.h"
#include "PluginScanner.h"
#include "StripAudioActions.h"
#include "UndoManager.h"

namespace fiddle {

StripAudioCommandService::StripAudioCommandService(MixerModel &mixer,
                                                   PluginScanner &scanner,
                                                   UndoManager &undoManager)
    : mixer_(mixer), scanner_(scanner), undoManager_(undoManager) {}

juce::var
StripAudioCommandService::state(const juce::String &stripId) const {
  if (auto *strip = mixer_.getStrip(stripId)) {
    auto result = strip->audioEngine().toJson();
    if (auto *object = result.getDynamicObject()) {
      object->setProperty("stripId", stripId);
      object->setProperty("stripName",
                          strip->layerName.isNotEmpty() ? strip->layerName
                                                       : strip->library);
      object->setProperty("instrumentName",
                          strip->hasPlugin() ? juce::String("VST instrument")
                                             : juce::String("No instrument"));
    }
    return result;
  }
  return {};
}

bool StripAudioCommandService::addInsert(const juce::String &stripId,
                                         int pluginUid,
                                         StripInsertPosition position) {
  if (!mixer_.getStrip(stripId))
    return false;
  for (const auto &description : scanner_.getKnownPluginList().getTypes()) {
    if (description.uniqueId != pluginUid)
      continue;
    if (!PluginCompatibility::fromDescription(description,
                                              PluginSlotRole::effect)
             .compatible)
      return false;
    undoManager_.perform(std::make_unique<AddStripInsertAction>(
        mixer_, stripId, description, position));
    return true;
  }
  return false;
}

bool StripAudioCommandService::removeInsert(const juce::String &stripId,
                                            const juce::String &slotId) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip || !strip->audioEngine().positionOf(slotId))
    return false;
  undoManager_.perform(
      std::make_unique<RemoveStripInsertAction>(mixer_, stripId, slotId));
  return true;
}

bool StripAudioCommandService::moveInsert(const juce::String &stripId,
                                          const juce::String &slotId,
                                          StripInsertPosition position,
                                          int newIndex) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip || !strip->audioEngine().positionOf(slotId) || newIndex < 0 ||
      newIndex > strip->audioEngine().insertCount(position))
    return false;
  undoManager_.perform(std::make_unique<MoveStripInsertAction>(
      mixer_, stripId, slotId, position, newIndex));
  return true;
}

bool StripAudioCommandService::setInsertBypassed(
    const juce::String &stripId, const juce::String &slotId, bool bypassed) {
  auto *strip = mixer_.getStrip(stripId);
  const auto snapshot =
      strip ? strip->audioEngine().snapshot(slotId) : std::nullopt;
  if (!snapshot)
    return false;
  undoManager_.perform(std::make_unique<BypassStripInsertAction>(
      mixer_, stripId, slotId, snapshot->bypassed, bypassed));
  return true;
}

bool StripAudioCommandService::showInsertEditor(
    const juce::String &stripId, const juce::String &slotId) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return false;
  const auto title = strip->layerName.isNotEmpty() ? strip->layerName
                                                   : strip->library;
  return strip->audioEngine().showEditor(slotId, title);
}

bool StripAudioCommandService::toggleInsertEditor(
    const juce::String &stripId, const juce::String &slotId) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return false;
  const auto title = strip->layerName.isNotEmpty() ? strip->layerName
                                                   : strip->library;
  return strip->audioEngine().toggleEditor(slotId, title);
}

} // namespace fiddle
