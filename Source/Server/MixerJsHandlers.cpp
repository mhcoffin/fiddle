#include "MixerJsHandlers.h"

#include "MessageRouter.h"
#include "MixerCommands.h"

#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

namespace fiddle {
namespace {

juce::Array<juce::var> payloadArguments(const juce::var &payload) {
  if (auto *arguments = payload.getArray())
    return *arguments;
  return {};
}

std::vector<juce::String> parseStripIds(const juce::var &value) {
  const auto parsed = juce::JSON::parse(value.toString());
  const auto *array = parsed.getArray();
  if (array == nullptr)
    return {};

  std::vector<juce::String> stripIds;
  stripIds.reserve(static_cast<std::size_t>(array->size()));
  for (const auto &id : *array)
    stripIds.push_back(id.toString());
  return stripIds;
}

} // namespace

MixerJsHandlers::MixerJsHandlers(MessageRouter &router, MixerCommands &commands,
                                 Callbacks callbacks)
    : router_(router), commands_(commands), callbacks_(std::move(callbacks)) {}

void MixerJsHandlers::dispatch(Task task) {
  if (callbacks_.dispatch)
    callbacks_.dispatch(std::move(task));
  else
    task();
}

void MixerJsHandlers::notifyChanged(bool changed, bool persist) {
  if (!changed)
    return;
  if (persist && callbacks_.persistMixer)
    callbacks_.persistMixer();
  if (callbacks_.mixerChanged)
    callbacks_.mixerChanged();
}

void MixerJsHandlers::registerHandlers() {
  router_.registerHandler("addMixerStrip", [this](const juce::var &) {
    dispatch([this] { notifyChanged(commands_.addStrip(), false); });
  });

  router_.registerHandler("duplicateStripInput", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 1)
      return;
    const auto stripId = arguments[0].toString();
    dispatch([this, stripId] {
      notifyChanged(commands_.duplicateStrip(stripId), false);
    });
  });

  router_.registerHandler("removeMixerStrip", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 1)
      return;
    const auto stripId = arguments[0].toString();
    dispatch([this, stripId] {
      notifyChanged(commands_.removeStrip(stripId), false);
    });
  });

  router_.registerHandler("setStripLibrary", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripId = arguments[0].toString();
    const auto library = arguments[1].toString();
    dispatch([this, stripId, library] {
      notifyChanged(commands_.setLibrary(stripId, library), false);
    });
  });

  router_.registerHandler("setStripInput", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 3)
      return;
    const auto stripId = arguments[0].toString();
    const auto port = static_cast<int>(arguments[1]);
    const auto channel = static_cast<int>(arguments[2]);
    dispatch([this, stripId, port, channel] {
      notifyChanged(commands_.setInput(stripId, port, channel), false);
    });
  });

  router_.registerHandler("setStripGain", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripId = arguments[0].toString();
    const auto gainDb = juce::jlimit(
        -120.0f, 6.0f, static_cast<float>(static_cast<double>(arguments[1])));
    dispatch([this, stripId, gainDb] {
      notifyChanged(commands_.setGain(stripId, gainDb), false);
    });
  });

  router_.registerHandler("setStripMute", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripId = arguments[0].toString();
    const auto muted = static_cast<bool>(arguments[1]);
    dispatch([this, stripId, muted] {
      notifyChanged(commands_.setMute(stripId, muted), true);
    });
  });

  router_.registerHandler("setStripSolo", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripId = arguments[0].toString();
    const auto soloed = static_cast<bool>(arguments[1]);
    dispatch([this, stripId, soloed] {
      notifyChanged(commands_.setSolo(stripId, soloed), true);
    });
  });

  router_.registerHandler("toggleLibraryActive", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 1)
      return;
    const auto library = arguments[0].toString();
    dispatch([this, library] {
      notifyChanged(commands_.toggleLibraryActive(library), true);
    });
  });

  router_.registerHandler("setStripProgram", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripId = arguments[0].toString();
    const auto programIndex = static_cast<int>(arguments[1]);
    dispatch([this, stripId, programIndex] {
      notifyChanged(commands_.setProgram(stripId, programIndex), false);
    });
  });

  router_.registerHandler("setGroupGainDelta", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripIds = parseStripIds(arguments[0]);
    if (stripIds.empty())
      return;
    const auto deltaDb =
        static_cast<float>(static_cast<double>(arguments[1]));
    dispatch([this, stripIds, deltaDb] {
      notifyChanged(commands_.setGroupGainDelta(stripIds, deltaDb), false);
    });
  });

  router_.registerHandler("setGroupGainAbsolute",
                          [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripIds = parseStripIds(arguments[0]);
    if (stripIds.empty())
      return;
    const auto gainDb = juce::jlimit(
        -120.0f, 6.0f, static_cast<float>(static_cast<double>(arguments[1])));
    dispatch([this, stripIds, gainDb] {
      notifyChanged(commands_.setGroupGainAbsolute(stripIds, gainDb), false);
    });
  });

  router_.registerHandler("setGroupLibrary", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripIds = parseStripIds(arguments[0]);
    if (stripIds.empty())
      return;
    const auto library = arguments[1].toString();
    dispatch([this, stripIds, library] {
      notifyChanged(commands_.setGroupLibrary(stripIds, library), false);
    });
  });

  router_.registerHandler("removeGroupStrips", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 1)
      return;
    const auto stripIds = parseStripIds(arguments[0]);
    if (stripIds.empty())
      return;
    dispatch([this, stripIds] {
      notifyChanged(commands_.removeGroupStrips(stripIds), false);
    });
  });
}

} // namespace fiddle
