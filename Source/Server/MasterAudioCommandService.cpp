#include "MasterAudioCommandService.h"

#include "MasterAudioActions.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoManager.h"

namespace fiddle {

MasterAudioCommandService::MasterAudioCommandService(MixerModel &mixer,
                                                     PluginScanner &scanner,
                                                     UndoManager &undoManager)
    : mixer_(mixer), scanner_(scanner), undoManager_(undoManager) {}

juce::var MasterAudioCommandService::state() const {
  return mixer_.masterAudio().toJson();
}

bool MasterAudioCommandService::addInsert(int pluginUid) {
  for (const auto &description : scanner_.getKnownPluginList().getTypes()) {
    if (description.uniqueId != pluginUid)
      continue;
    if (!PluginCompatibility::fromDescription(description,
                                              PluginSlotRole::effect)
             .compatible)
      return false;
    undoManager_.perform(
        std::make_unique<AddMasterInsertAction>(mixer_, description));
    return true;
  }
  return false;
}

bool MasterAudioCommandService::removeInsert(const juce::String &slotId) {
  if (mixer_.masterAudio().indexOf(slotId) < 0)
    return false;
  undoManager_.perform(
      std::make_unique<RemoveMasterInsertAction>(mixer_, slotId));
  return true;
}

bool MasterAudioCommandService::moveInsert(const juce::String &slotId,
                                           int newIndex) {
  if (mixer_.masterAudio().indexOf(slotId) < 0 || newIndex < 0 ||
      newIndex >= mixer_.masterAudio().insertCount())
    return false;
  undoManager_.perform(
      std::make_unique<MoveMasterInsertAction>(mixer_, slotId, newIndex));
  return true;
}

bool MasterAudioCommandService::setInsertBypassed(const juce::String &slotId,
                                                  bool bypassed) {
  const auto snapshot = mixer_.masterAudio().snapshot(slotId);
  if (!snapshot)
    return false;
  undoManager_.perform(std::make_unique<BypassMasterInsertAction>(
      mixer_, slotId, snapshot->bypassed, bypassed));
  return true;
}

bool MasterAudioCommandService::showInsertEditor(const juce::String &slotId) {
  return mixer_.masterAudio().showEditor(slotId);
}

bool MasterAudioCommandService::setGainDb(float gainDb) {
  undoManager_.perform(std::make_unique<SetMasterGainAction>(
      mixer_, mixer_.masterAudio().gainDb(), gainDb));
  return true;
}

} // namespace fiddle
