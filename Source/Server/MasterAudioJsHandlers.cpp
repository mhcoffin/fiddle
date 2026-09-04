#include "MasterAudioJsHandlers.h"

#include "MasterAudioCommands.h"
#include "MessageRouter.h"

#include <utility>

namespace fiddle {
namespace {

juce::Array<juce::var> arguments(const juce::var &payload) {
  if (const auto *array = payload.getArray())
    return *array;
  return {};
}

} // namespace

MasterAudioJsHandlers::MasterAudioJsHandlers(MessageRouter &router,
                                             MasterAudioCommands &commands,
                                             Callbacks callbacks)
    : router_(router), commands_(commands), callbacks_(std::move(callbacks)) {}

void MasterAudioJsHandlers::dispatch(Task task) {
  if (callbacks_.dispatch)
    callbacks_.dispatch(std::move(task));
  else
    task();
}

void MasterAudioJsHandlers::registerHandlers() {
  router_.registerHandler("requestMasterAudioState", [this](const juce::var &) {
    dispatch([this] {
      if (callbacks_.publishState)
        callbacks_.publishState(commands_.state());
    });
  });

  router_.registerHandler("addMasterInsert", [this](const juce::var &payload) {
    const auto args = arguments(payload);
    if (args.isEmpty())
      return;
    const int pluginUid = static_cast<int>(args[0]);
    dispatch([this, pluginUid] { commands_.addInsert(pluginUid); });
  });

  router_.registerHandler(
      "removeMasterInsert", [this](const juce::var &payload) {
        const auto args = arguments(payload);
        if (args.isEmpty())
          return;
        const auto slotId = args[0].toString();
        dispatch([this, slotId] { commands_.removeInsert(slotId); });
      });

  router_.registerHandler("moveMasterInsert", [this](const juce::var &payload) {
    const auto args = arguments(payload);
    if (args.size() < 2)
      return;
    const auto slotId = args[0].toString();
    const int index = static_cast<int>(args[1]);
    dispatch([this, slotId, index] { commands_.moveInsert(slotId, index); });
  });

  router_.registerHandler("setMasterInsertBypassed",
                          [this](const juce::var &payload) {
                            const auto args = arguments(payload);
                            if (args.size() < 2)
                              return;
                            const auto slotId = args[0].toString();
                            const bool bypassed = static_cast<bool>(args[1]);
                            dispatch([this, slotId, bypassed] {
                              commands_.setInsertBypassed(slotId, bypassed);
                            });
                          });

  router_.registerHandler(
      "showMasterInsertEditor", [this](const juce::var &payload) {
        const auto args = arguments(payload);
        if (args.isEmpty())
          return;
        const auto slotId = args[0].toString();
        dispatch([this, slotId] { commands_.showInsertEditor(slotId); });
      });

  router_.registerHandler("setMasterGain", [this](const juce::var &payload) {
    const auto args = arguments(payload);
    if (args.isEmpty())
      return;
    const float gainDb = static_cast<float>(static_cast<double>(args[0]));
    dispatch([this, gainDb] { commands_.setGainDb(gainDb); });
  });
}

} // namespace fiddle
