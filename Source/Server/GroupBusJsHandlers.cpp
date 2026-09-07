#include "GroupBusJsHandlers.h"

#include "GroupBusCommands.h"
#include "MessageRouter.h"

namespace fiddle {
namespace {
juce::Array<juce::var> args(const juce::var &payload) {
  if (auto *array = payload.getArray())
    return *array;
  return {};
}

std::vector<juce::String> stripIds(const juce::var &value) {
  const auto parsed = juce::JSON::parse(value.toString());
  std::vector<juce::String> result;
  if (auto *array = parsed.getArray())
    for (const auto &item : *array)
      result.push_back(item.toString());
  return result;
}
} // namespace

GroupBusJsHandlers::GroupBusJsHandlers(MessageRouter &router,
                                       GroupBusCommands &commands,
                                       Callbacks callbacks)
    : router_(router), commands_(commands), callbacks_(std::move(callbacks)) {}

void GroupBusJsHandlers::dispatch(Task task) {
  if (callbacks_.dispatch)
    callbacks_.dispatch(std::move(task));
  else
    task();
}

void GroupBusJsHandlers::notify(bool changed) {
  if (changed && callbacks_.changed)
    callbacks_.changed();
}

void GroupBusJsHandlers::registerHandlers() {
  router_.registerHandler("requestGroupBusState", [this](const juce::var &) {
    dispatch([this] {
      if (callbacks_.requested)
        callbacks_.requested();
    });
  });
  router_.registerHandler("addGroupBus", [this](const juce::var &payload) {
    const auto a = args(payload);
    if (a.isEmpty()) return;
    const auto name = a[0].toString();
    const auto ids = a.size() > 1 ? stripIds(a[1]) : std::vector<juce::String>{};
    dispatch([this, name, ids] { notify(commands_.addGroupBus(name, ids)); });
  });
  router_.registerHandler("removeGroupBus", [this](const juce::var &payload) {
    const auto a = args(payload); if (a.isEmpty()) return;
    const auto id = a[0].toString();
    dispatch([this, id] { notify(commands_.removeGroupBus(id)); });
  });
  router_.registerHandler("renameGroupBus", [this](const juce::var &payload) {
    const auto a = args(payload); if (a.size() < 2) return;
    const auto id = a[0].toString(), name = a[1].toString();
    dispatch([this, id, name] { notify(commands_.renameGroupBus(id, name)); });
  });
  router_.registerHandler("moveGroupBus", [this](const juce::var &payload) {
    const auto a = args(payload); if (a.size() < 2) return;
    const auto id = a[0].toString(); const int index = static_cast<int>(a[1]);
    dispatch([this, id, index] { notify(commands_.moveGroupBus(id, index)); });
  });
  router_.registerHandler("setGroupBusGain", [this](const juce::var &payload) {
    const auto a = args(payload); if (a.size() < 2) return;
    const auto id = a[0].toString(); const float gain = static_cast<float>((double)a[1]);
    dispatch([this, id, gain] { notify(commands_.setGroupBusGain(id, gain)); });
  });
  router_.registerHandler("setGroupBusMute", [this](const juce::var &payload) {
    const auto a = args(payload); if (a.size() < 2) return;
    const auto id = a[0].toString(); const bool value = static_cast<bool>(a[1]);
    dispatch([this, id, value] { notify(commands_.setGroupBusMute(id, value)); });
  });
  router_.registerHandler("setGroupBusSolo", [this](const juce::var &payload) {
    const auto a = args(payload); if (a.size() < 2) return;
    const auto id = a[0].toString(); const bool value = static_cast<bool>(a[1]);
    dispatch([this, id, value] { notify(commands_.setGroupBusSolo(id, value)); });
  });
  router_.registerHandler("setStripDirectOutput", [this](const juce::var &payload) {
    const auto a = args(payload); if (a.size() < 2) return;
    const auto strip = a[0].toString(), bus = a[1].toString();
    dispatch([this, strip, bus] { notify(commands_.setStripDirectOutput(strip, bus)); });
  });
}
} // namespace fiddle
