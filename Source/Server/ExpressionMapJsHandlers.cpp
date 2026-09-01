#include "ExpressionMapJsHandlers.h"

#include "ExpressionMapCommands.h"
#include "MessageRouter.h"

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

ExpressionMapJsHandlers::ExpressionMapJsHandlers(
    MessageRouter &router, ExpressionMapCommands &commands,
    Callbacks callbacks)
    : router_(router), commands_(commands), callbacks_(std::move(callbacks)) {}

void ExpressionMapJsHandlers::dispatch(Task task) {
  if (callbacks_.dispatch)
    callbacks_.dispatch(std::move(task));
  else
    task();
}

void ExpressionMapJsHandlers::registerHandlers() {
  router_.registerHandler("loadExpressionMap",
                          [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripId = arguments[0].toString();
    const auto entityId = arguments[1].toString();
    dispatch([this, stripId, entityId] {
      if (commands_.assign(stripId, entityId) && callbacks_.mixerChanged)
        callbacks_.mixerChanged();
    });
  });

  router_.registerHandler("clearExpressionMap",
                          [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 1)
      return;
    const auto stripId = arguments[0].toString();
    dispatch([this, stripId] {
      if (commands_.clear(stripId) && callbacks_.mixerChanged)
        callbacks_.mixerChanged();
    });
  });

  router_.registerHandler("loadExpressionMapFromFile",
                          [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 1 || !callbacks_.chooseFile)
      return;
    const auto stripId = arguments[0].toString();
    callbacks_.chooseFile([this, stripId](juce::File file) {
      if (file == juce::File{})
        return;
      dispatch([this, stripId, file] {
        if (commands_.importFile(stripId, file) && callbacks_.mixerChanged)
          callbacks_.mixerChanged();
      });
    });
  });

  router_.registerHandler("requestExpressionMaps",
                          [this](const juce::var &) {
    dispatch([this] {
      if (callbacks_.publishCatalog)
        callbacks_.publishCatalog(commands_.catalog());
    });
  });

  router_.registerHandler("setGroupExpressionMap",
                          [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripIds = parseStripIds(arguments[0]);
    if (stripIds.empty())
      return;
    const auto entityId = arguments[1].toString();
    dispatch([this, stripIds, entityId] {
      if (commands_.assignGroup(stripIds, entityId) &&
          callbacks_.mixerChanged)
        callbacks_.mixerChanged();
    });
  });
}

} // namespace fiddle
