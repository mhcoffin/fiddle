#pragma once

#include "PluginEditorWindow.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace fiddle {

/// Manages multiple loaded VST3 plugin instances, each with its own editor
/// window.  Plugins are identified by a string slot ID (typically the plugin
/// UID converted to string).
class PluginHost {
public:
  PluginHost() { formatManager_.addFormat(new juce::VST3PluginFormat()); }

  ~PluginHost() { unloadAll(); }

  /// A single loaded plugin slot.
  struct Slot {
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<PluginEditorWindow> editorWindow;
    juce::String name;
  };

  /// Load a plugin from a description. Instantiation happens asynchronously.
  void loadPlugin(const juce::String &slotId,
                  const juce::PluginDescription &desc,
                  std::function<void(bool)> onComplete = nullptr) {
    // Unload existing plugin in this slot (if any)
    unloadPlugin(slotId);

    formatManager_.createPluginInstanceAsync(
        desc, 44100.0, 512,
        [this, slotId, desc,
         onComplete](std::unique_ptr<juce::AudioPluginInstance> instance,
                     const juce::String &error) {
          if (!instance) {
            std::cerr << "[PluginHost] Failed to load " << desc.name << ": "
                      << error << std::endl;
            if (onComplete)
              onComplete(false);
            return;
          }

          instance->prepareToPlay(44100.0, 512);
          std::cerr << "[PluginHost] Loaded (Async): " << desc.name
                    << std::endl;

          auto slot = std::make_unique<Slot>();
          slot->instance = std::move(instance);
          slot->name = desc.name;

          // Open editor window
          if (auto *editor = slot->instance->createEditor()) {
            slot->editorWindow = std::make_unique<PluginEditorWindow>(
                desc.name, editor, [this, id = slotId]() {
                  // Editor window was closed — just hide, don't unload
                  std::cerr << "[PluginHost] Editor window closed for slot "
                            << id << std::endl;
                });
          }

          slots_[slotId] = std::move(slot);
          if (onComplete)
            onComplete(true);
        });
  }

  /// Unload a plugin from a slot.
  void unloadPlugin(const juce::String &slotId) {
    auto it = slots_.find(slotId);
    if (it == slots_.end())
      return;

    auto &slot = it->second;
    // Close editor first
    if (slot->editorWindow)
      slot->editorWindow.reset();
    // Release processor
    if (slot->instance) {
      slot->instance->releaseResources();
      slot->instance.reset();
    }
    slots_.erase(it);
    std::cerr << "[PluginHost] Unloaded slot: " << slotId << std::endl;
  }

  /// Unload all plugins.
  void unloadAll() {
    // Copy keys to avoid iterator invalidation
    std::vector<juce::String> keys;
    for (auto &pair : slots_)
      keys.push_back(pair.first);
    for (auto &key : keys)
      unloadPlugin(key);
  }

  /// Show the editor window for a loaded plugin.
  void showEditor(const juce::String &slotId) {
    auto it = slots_.find(slotId);
    if (it == slots_.end() || !it->second->instance)
      return;

    auto &slot = it->second;
    if (slot->editorWindow) {
      slot->editorWindow->setVisible(true);
      slot->editorWindow->toFront(true);
    } else if (auto *editor = slot->instance->createEditor()) {
      slot->editorWindow =
          std::make_unique<PluginEditorWindow>(slot->name, editor);
    }
  }

  /// Check if a slot has a loaded plugin.
  bool isLoaded(const juce::String &slotId) const {
    return slots_.count(slotId) > 0;
  }

  /// Get all loaded slot IDs as a JSON array of
  /// { slotId, name }
  juce::String getLoadedPluginsAsJson() const {
    juce::Array<juce::var> arr;
    for (auto &pair : slots_) {
      auto *obj = new juce::DynamicObject();
      obj->setProperty("slotId", pair.first);
      obj->setProperty("name", pair.second->name);
      arr.add(juce::var(obj));
    }
    return juce::JSON::toString(juce::var(arr), true);
  }

  int getLoadedCount() const { return static_cast<int>(slots_.size()); }

private:
  juce::AudioPluginFormatManager formatManager_;
  std::map<juce::String, std::unique_ptr<Slot>> slots_;
};

} // namespace fiddle
