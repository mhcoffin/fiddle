#include "StripAudioJsHandlers.h"

#include "MessageRouter.h"
#include "StripAudioCommands.h"

#include <optional>
#include <utility>

namespace fiddle {
namespace {

juce::Array<juce::var> arguments(const juce::var &payload) {
  if (const auto *array = payload.getArray())
    return *array;
  return {};
}

std::optional<StripInsertPosition> positionFrom(const juce::var &value) {
  if (value.toString() == "preFader")
    return StripInsertPosition::preFader;
  if (value.toString() == "postFader")
    return StripInsertPosition::postFader;
  return std::nullopt;
}

} // namespace

StripAudioJsHandlers::StripAudioJsHandlers(MessageRouter &router,
                                           StripAudioCommands &commands,
                                           Callbacks callbacks)
    : router_(router), commands_(commands), callbacks_(std::move(callbacks)) {}

void StripAudioJsHandlers::dispatch(Task task) {
  if (callbacks_.dispatch)
    callbacks_.dispatch(std::move(task));
  else
    task();
}

void StripAudioJsHandlers::registerHandlers() {
  router_.registerHandler("requestStripAudioState",
                          [this](const juce::var &payload) {
                            const auto args = arguments(payload);
                            if (args.isEmpty())
                              return;
                            const auto stripId = args[0].toString();
                            dispatch([this, stripId] {
                              if (callbacks_.publishState)
                                callbacks_.publishState(commands_.state(stripId));
                            });
                          });

  router_.registerHandler("addStripInsert", [this](const juce::var &payload) {
    const auto args = arguments(payload);
    if (args.size() < 3)
      return;
    const auto stripId = args[0].toString();
    const int pluginUid = static_cast<int>(args[1]);
    const auto position = positionFrom(args[2]);
    if (!position)
      return;
    dispatch([this, stripId, pluginUid, position = *position] {
      commands_.addInsert(stripId, pluginUid, position);
    });
  });

  router_.registerHandler("removeStripInsert",
                          [this](const juce::var &payload) {
                            const auto args = arguments(payload);
                            if (args.size() < 2)
                              return;
                            const auto stripId = args[0].toString();
                            const auto slotId = args[1].toString();
                            dispatch([this, stripId, slotId] {
                              commands_.removeInsert(stripId, slotId);
                            });
                          });

  router_.registerHandler("moveStripInsert", [this](const juce::var &payload) {
    const auto args = arguments(payload);
    if (args.size() < 4)
      return;
    const auto stripId = args[0].toString();
    const auto slotId = args[1].toString();
    const auto position = positionFrom(args[2]);
    if (!position)
      return;
    const int index = static_cast<int>(args[3]);
    dispatch([this, stripId, slotId, position = *position, index] {
      commands_.moveInsert(stripId, slotId, position, index);
    });
  });

  router_.registerHandler("setStripInsertBypassed",
                          [this](const juce::var &payload) {
                            const auto args = arguments(payload);
                            if (args.size() < 3)
                              return;
                            const auto stripId = args[0].toString();
                            const auto slotId = args[1].toString();
                            const bool bypassed = static_cast<bool>(args[2]);
                            dispatch([this, stripId, slotId, bypassed] {
                              commands_.setInsertBypassed(stripId, slotId,
                                                          bypassed);
                            });
                          });

  router_.registerHandler("showStripInsertEditor",
                          [this](const juce::var &payload) {
                            const auto args = arguments(payload);
                            if (args.size() < 2)
                              return;
                            const auto stripId = args[0].toString();
                            const auto slotId = args[1].toString();
                            dispatch([this, stripId, slotId] {
                              commands_.showInsertEditor(stripId, slotId);
                              if (callbacks_.publishState)
                                callbacks_.publishState(
                                    commands_.state(stripId));
                            });
                          });

  router_.registerHandler("toggleStripInsertEditor",
                          [this](const juce::var &payload) {
                            const auto args = arguments(payload);
                            if (args.size() < 2)
                              return;
                            const auto stripId = args[0].toString();
                            const auto slotId = args[1].toString();
                            dispatch([this, stripId, slotId] {
                              commands_.toggleInsertEditor(stripId, slotId);
                              if (callbacks_.publishState)
                                callbacks_.publishState(
                                    commands_.state(stripId));
                            });
                          });
}

} // namespace fiddle
