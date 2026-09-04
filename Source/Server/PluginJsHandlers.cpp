#include "PluginJsHandlers.h"

#include "MessageRouter.h"
#include "PluginCommands.h"

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

PluginJsHandlers::PluginJsHandlers(MessageRouter &router,
                                   PluginCommands &commands,
                                   Callbacks callbacks)
    : router_(router), commands_(commands), callbacks_(std::move(callbacks)) {}

void PluginJsHandlers::dispatch(Task task) {
  if (callbacks_.dispatch)
    callbacks_.dispatch(std::move(task));
  else
    task();
}

void PluginJsHandlers::beginScan(bool fullRescan) {
  const auto callbacks = callbacks_;
  const auto accepted = commands_.beginScan(
      fullRescan, [callbacks, fullRescan](PluginCatalogState state) {
        if (callbacks.logMessage) {
          callbacks.logMessage(
              fullRescan
                  ? "<b>[Plugins]</b> Rescan complete: " +
                        juce::String(state.count) + " plugins found"
                  : "<b>[Plugins]</b> Scan complete: " +
                        juce::String(state.count) + " plugins found");
        }
        if (callbacks.publishPluginList)
          callbacks.publishPluginList(state.plugins);
      });

  if (!accepted || !callbacks_.logMessage)
    return;

  callbacks_.logMessage(
      fullRescan
          ? "<b>[Plugins]</b> Full rescan of VST3 plugins..."
          : "<b>[Plugins]</b> Scanning for VST3 plugins (incremental)...");
}

void PluginJsHandlers::registerHandlers() {
  router_.registerHandler("scanPlugins",
                          [this](const juce::var &) { beginScan(false); });
  router_.registerHandler("rescanPlugins",
                          [this](const juce::var &) { beginScan(true); });

  router_.registerHandler("setStripPlugin", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;

    const auto stripId = arguments[0].toString();
    const auto pluginUid = static_cast<int>(arguments[1]);
    if (!commands_.isPluginAvailable(pluginUid))
      return;

    const auto onComplete = callbacks_.mixerChanged;
    dispatch([this, stripId, pluginUid, onComplete] {
      commands_.setPlugin(stripId, pluginUid, onComplete);
    });
  });

  router_.registerHandler("showStripEditor", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 1)
      return;
    const auto stripId = arguments[0].toString();
    dispatch([this, stripId] { commands_.showEditor(stripId); });
  });

  router_.registerHandler("restoreLibraryPluginState",
                          [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 4)
      return;
    const auto stripId = arguments[0].toString();
    const auto libraryId = arguments[1].toString();
    const auto entityId = arguments[2].toString();
    const auto instanceNumber = static_cast<int>(arguments[3]);
    dispatch([this, stripId, libraryId, entityId, instanceNumber] {
      commands_.restoreLibraryState(stripId, libraryId, entityId,
                                    instanceNumber);
    });
  });

  router_.registerHandler("restoreLibraryPatchState",
                          [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripId = arguments[0].toString();
    const auto patchId = arguments[1].toString();
    dispatch([this, stripId, patchId] {
      commands_.restoreLibraryPatchState(stripId, patchId);
    });
  });

  router_.registerHandler("requestPluginsState",
                          [this](const juce::var &) {
    dispatch([this] {
      const auto state = commands_.catalogState();
      if (callbacks_.publishScanning)
        callbacks_.publishScanning(state.scanning);
      if (state.count > 0 && callbacks_.publishPluginList)
        callbacks_.publishPluginList(state.plugins);
    });
  });

  router_.registerHandler("setGroupPlugin", [this](const juce::var &payload) {
    const auto arguments = payloadArguments(payload);
    if (arguments.size() < 2)
      return;
    const auto stripIds = parseStripIds(arguments[0]);
    if (stripIds.empty())
      return;
    const auto pluginUid = static_cast<int>(arguments[1]);
    if (!commands_.isPluginAvailable(pluginUid))
      return;

    const auto onComplete = callbacks_.mixerChanged;
    dispatch([this, stripIds, pluginUid, onComplete] {
      commands_.setGroupPlugin(stripIds, pluginUid, onComplete);
    });
  });
}

} // namespace fiddle
