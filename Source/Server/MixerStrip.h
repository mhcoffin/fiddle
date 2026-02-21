#pragma once

#include "PluginEditorWindow.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <vector>

namespace fiddle {

/// A single mixer channel strip. Owns a plugin instance + optional editor
/// window. Identified by a unique string ID.
struct MixerStrip {
  juce::String id;
  juce::String name;
  juce::String family; // Instrument family (e.g. "Strings", "Brass")

  // Input assignment (-1 = unassigned)
  int inputPort = -1;
  int inputChannel = -1;

  // Plugin
  int pluginUid = 0; // scanned plugin uniqueId (0 = none)
  std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
  std::unique_ptr<PluginEditorWindow> editorWindow;

  std::mutex processMutex;
  std::mutex midiMutex;
  std::vector<std::pair<double, juce::MidiMessage>> delayedMessages;
  double currentSampleRate = 44100.0;
  int currentBlockSize = 512;

  juce::AudioBuffer<float> tempBuffer;

  void prepareToPlay(double sampleRate, int blockSize) {
    std::lock_guard<std::mutex> lock(processMutex);
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    if (pluginInstance) {
      int maxChannels =
          juce::jmax(pluginInstance->getTotalNumInputChannels(),
                     pluginInstance->getTotalNumOutputChannels(), 2);
      tempBuffer.setSize(maxChannels, blockSize);
      pluginInstance->prepareToPlay(sampleRate, blockSize);
    }
  }

  void addDelayedMessage(double triggerTime, const juce::MidiMessage &msg) {
    std::lock_guard<std::mutex> lock(midiMutex);
    delayedMessages.push_back({triggerTime, msg});
  }

  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime) {
    juce::MidiBuffer midiBuffer;
    {
      std::lock_guard<std::mutex> lock(midiMutex);
      for (auto it = delayedMessages.begin(); it != delayedMessages.end();) {
        if (currentTime >= it->first) {
          std::cerr << "[MixerStrip " << id << "] Popped delayed event: len="
                    << it->second.getRawDataSize()
                    << ", timeDiff=" << (currentTime - it->first) << "ms"
                    << std::endl;
          midiBuffer.addEvent(it->second,
                              0); // Event fires effectively at sample 0
          it = delayedMessages.erase(it);
        } else {
          ++it;
        }
      }
    }

    std::lock_guard<std::mutex> lock(processMutex);
    if (pluginInstance) {
      int numSamples = audioBuffer.getNumSamples();

      // Safety check just in case tempBuffer isn't sized
      if (tempBuffer.getNumChannels() > 0 &&
          tempBuffer.getNumSamples() >= numSamples) {
        tempBuffer.clear();
        pluginInstance->processBlock(tempBuffer, midiBuffer);

        // Mix down (sum) output to the main host buffer
        int channelsToSum = juce::jmin((int)audioBuffer.getNumChannels(),
                                       tempBuffer.getNumChannels());
        for (int i = 0; i < channelsToSum; ++i) {
          audioBuffer.addFrom(i, 0, tempBuffer, i, 0, numSamples);
        }
      }
    }
  }

  /// Load a plugin from a description. Must be called on the message thread.
  void loadPlugin(const juce::PluginDescription &desc,
                  juce::AudioPluginFormatManager &formatManager,
                  std::function<void(bool)> onComplete = nullptr) {

    formatManager.createPluginInstanceAsync(
        desc, currentSampleRate, currentBlockSize,
        [this, desc,
         onComplete](std::unique_ptr<juce::AudioPluginInstance> instance,
                     const juce::String &error) {
          if (!instance) {
            std::cerr << "[MixerStrip " << id << "] Failed to load "
                      << desc.name << ": " << error << std::endl;
            if (onComplete)
              onComplete(false);
            return;
          }

          // Unload the old UI if valid
          editorWindow.reset();

          int maxChannels =
              juce::jmax(instance->getTotalNumInputChannels(),
                         instance->getTotalNumOutputChannels(), 2);

          instance->prepareToPlay(currentSampleRate, currentBlockSize);

          // We must safely swap the pointer inside the audio lock, but we
          // DO NOT want to destroy the old plugin inside the lock.
          std::unique_ptr<juce::AudioPluginInstance> oldPlugin;
          {
            std::lock_guard<std::mutex> lock(processMutex);
            tempBuffer.setSize(maxChannels, currentBlockSize);

            oldPlugin = std::move(pluginInstance);
            pluginInstance = std::move(instance);
            pluginUid = desc.uniqueId;
          }
          // oldPlugin is safely destroyed here, off the audio thread lock.
          // Editor is NOT opened here — user opens it via showEditor().

          std::cerr << "[MixerStrip " << id << "] Loaded (Async): " << desc.name
                    << std::endl;
          if (onComplete)
            onComplete(true);
        });
  }

  /// Unload the plugin and close editor.
  void unloadPlugin() {
    editorWindow.reset();

    std::lock_guard<std::mutex> lock(processMutex);
    if (pluginInstance) {
      pluginInstance->releaseResources();
      pluginInstance.reset();
    }
    pluginUid = 0;
  }

  /// Show the editor window (create if needed).
  void showEditor() {
    if (!pluginInstance)
      return;
    if (editorWindow) {
      editorWindow->setVisible(true);
      editorWindow->toFront(true);
    } else if (auto *editor = pluginInstance->createEditor()) {
      editorWindow = std::make_unique<PluginEditorWindow>(name, editor);
    }
  }

  /// Serialize to JSON var.
  juce::var toJson() const {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", id);
    obj->setProperty("name", name);
    obj->setProperty("family", family);
    obj->setProperty("inputPort", inputPort);
    obj->setProperty("inputChannel", inputChannel);
    obj->setProperty("pluginUid", pluginUid);
    obj->setProperty("hasPlugin", pluginInstance != nullptr);
    return juce::var(obj);
  }
};

} // namespace fiddle
