#include "MixerCommandService.h"

#include "MixerModel.h"
#include "UndoActions.h"
#include "UndoManager.h"

#include <memory>

namespace fiddle {

MixerCommandService::MixerCommandService(MixerModel &mixer,
                                         UndoManager &undoManager)
    : mixer_(mixer), undoManager_(undoManager) {}

bool MixerCommandService::addStrip() {
  undoManager_.perform(std::make_unique<AddStripAction>(mixer_));
  return true;
}

bool MixerCommandService::duplicateStrip(const juce::String &stripId) {
  if (mixer_.getStrip(stripId) == nullptr)
    return false;

  undoManager_.perform(
      std::make_unique<DuplicateStripAction>(mixer_, stripId));
  return true;
}

bool MixerCommandService::removeStrip(const juce::String &stripId) {
  if (mixer_.getStrip(stripId) == nullptr)
    return false;

  undoManager_.perform(std::make_unique<RemoveStripAction>(mixer_, stripId));
  return true;
}

bool MixerCommandService::setLibrary(const juce::String &stripId,
                                     const juce::String &library) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr)
    return false;

  undoManager_.perform(std::make_unique<SetLibraryAction>(
      mixer_, stripId, strip->library, library));
  return true;
}

bool MixerCommandService::setInput(const juce::String &stripId, int port,
                                   int channel) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr)
    return false;

  const auto oldState = strip->realtimeState();
  undoManager_.perform(std::make_unique<SetInputAction>(
      mixer_, stripId, oldState.inputPort, oldState.inputChannel, port,
      channel));
  return true;
}

bool MixerCommandService::setGain(const juce::String &stripId, float gainDb) {
  auto *strip = mixer_.getStrip(stripId);
  if (strip == nullptr)
    return false;

  undoManager_.perform(std::make_unique<SetGainAction>(
      mixer_, stripId, strip->gainDb(), juce::jlimit(-120.0f, 6.0f, gainDb)));
  return true;
}

bool MixerCommandService::setMute(const juce::String &stripId, bool muted) {
  return mixer_.setStripMute(stripId, muted);
}

bool MixerCommandService::setSolo(const juce::String &stripId, bool soloed) {
  return mixer_.setStripSolo(stripId, soloed);
}

bool MixerCommandService::toggleLibraryActive(const juce::String &library) {
  bool found = false;
  bool allActive = true;
  for (auto *strip : mixer_.getAllStrips()) {
    if (strip->library != library)
      continue;

    found = true;
    allActive = allActive && strip->isActive();
  }

  if (!found)
    return false;

  undoManager_.perform(std::make_unique<ToggleLibraryActiveAction>(
      mixer_, library, !allActive));
  return true;
}

bool MixerCommandService::setProgram(const juce::String &stripId,
                                     int programIndex) {
  auto *strip = mixer_.getStrip(stripId);
  return strip != nullptr && strip->setPluginProgram(programIndex);
}

bool MixerCommandService::setGroupGainDelta(
    const std::vector<juce::String> &stripIds, float deltaDb) {
  std::vector<std::unique_ptr<UndoableAction>> actions;
  for (const auto &stripId : stripIds) {
    if (auto *strip = mixer_.getStrip(stripId)) {
      const auto gainDb =
          juce::jlimit(-120.0f, 6.0f, strip->gainDb() + deltaDb);
      actions.push_back(std::make_unique<SetGainAction>(
          mixer_, stripId, strip->gainDb(), gainDb));
    }
  }

  if (actions.empty())
    return false;

  undoManager_.perform(std::make_unique<CompoundAction>(
      "Group gain delta", std::move(actions), "group-gain"));
  return true;
}

bool MixerCommandService::setGroupGainAbsolute(
    const std::vector<juce::String> &stripIds, float gainDb) {
  std::vector<std::unique_ptr<UndoableAction>> actions;
  gainDb = juce::jlimit(-120.0f, 6.0f, gainDb);
  for (const auto &stripId : stripIds) {
    if (auto *strip = mixer_.getStrip(stripId)) {
      actions.push_back(std::make_unique<SetGainAction>(
          mixer_, stripId, strip->gainDb(), gainDb));
    }
  }

  if (actions.empty())
    return false;

  undoManager_.perform(std::make_unique<CompoundAction>(
      "Group gain absolute", std::move(actions)));
  return true;
}

bool MixerCommandService::setGroupLibrary(
    const std::vector<juce::String> &stripIds, const juce::String &library) {
  std::vector<std::unique_ptr<UndoableAction>> actions;
  for (const auto &stripId : stripIds) {
    if (auto *strip = mixer_.getStrip(stripId)) {
      actions.push_back(std::make_unique<SetLibraryAction>(
          mixer_, stripId, strip->library, library));
    }
  }

  if (actions.empty())
    return false;

  undoManager_.perform(std::make_unique<CompoundAction>(
      "Group set library", std::move(actions)));
  return true;
}

bool MixerCommandService::removeGroupStrips(
    const std::vector<juce::String> &stripIds) {
  std::vector<std::unique_ptr<UndoableAction>> actions;
  for (const auto &stripId : stripIds) {
    if (mixer_.getStrip(stripId) != nullptr) {
      actions.push_back(std::make_unique<RemoveStripAction>(mixer_, stripId));
    }
  }

  if (actions.empty())
    return false;

  undoManager_.perform(std::make_unique<CompoundAction>(
      "Group remove strips", std::move(actions)));
  return true;
}

} // namespace fiddle
