#pragma once

#include "ExpressionMapData.h"
#include "PluginEditorWindow.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <mutex>
#include <vector>

namespace fiddle {

/// A single mixer channel strip. Owns a plugin instance + optional editor
/// window. Identified by a unique string ID.
struct MixerStrip {
  juce::String id;
  juce::String library; // User-supplied VST library label (e.g. "SSP")
  juce::String family;  // Instrument family (e.g. "Strings", "Brass")
  bool isSolo = true;   // true = solo player, false = section

  // Input assignment (-1 = unassigned)
  int inputPort = -1;
  int inputChannel = -1;

  // Plugin
  int pluginUid = 0; // scanned plugin uniqueId (0 = none)
  std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
  std::unique_ptr<PluginEditorWindow> editorWindow;

  // Expression map
  std::shared_ptr<ExpressionMapData> expressionMap;
  juce::String expressionMapPath; // file path for persistence

  std::mutex processMutex;
  std::mutex midiMutex;
  std::vector<std::pair<double, juce::MidiMessage>> delayedMessages;
  double currentSampleRate = 44100.0;
  int currentBlockSize = 512;

  /// Fader gain in dB. Atomic for lock-free audio thread reads.
  /// Range: +6 dB to -120 dB. Values <= -120 treated as silence.
  std::atomic<float> gainDb{0.0f};

  /// Peak level in dB (post-fader). Lock-free, written by audio thread.
  std::atomic<float> peakDb{-120.0f};

  /// Peak-hold level in dB. Decays slowly (~5 dB/sec) for visual indicator.
  std::atomic<float> peakHoldDb{-120.0f};

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

  /// Send All Notes Off + All Sound Off + Reset All Controllers to the plugin.
  /// Also clears pending delayed messages to prevent stale notes from firing.
  void allNotesOff() {
    {
      std::lock_guard<std::mutex> lock(midiMutex);
      delayedMessages.clear();
    }
    std::lock_guard<std::mutex> lock(processMutex);
    if (!pluginInstance)
      return;
    juce::MidiBuffer panic;
    for (int ch = 1; ch <= 16; ++ch) {
      panic.addEvent(juce::MidiMessage::allSoundOff(ch), 0);
      panic.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
      panic.addEvent(juce::MidiMessage::allControllersOff(ch), 0);
    }
    juce::AudioBuffer<float> dummy(tempBuffer.getNumChannels(),
                                   currentBlockSize);
    dummy.clear();
    pluginInstance->processBlock(dummy, panic);
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

        // Apply fader gain and mix down to the main host buffer
        float db = gainDb.load(std::memory_order_relaxed);
        if (db <= -120.0f) {
          // Silence — skip summing, decay peak
          float decay = 20.0f * numSamples / (float)currentSampleRate;
          float holdDecay = 3.0f * numSamples / (float)currentSampleRate;
          float prev = peakDb.load(std::memory_order_relaxed);
          peakDb.store(juce::jmax(-120.0f, prev - decay),
                       std::memory_order_relaxed);
          float prevHold = peakHoldDb.load(std::memory_order_relaxed);
          peakHoldDb.store(juce::jmax(-120.0f, prevHold - holdDecay),
                           std::memory_order_relaxed);
        } else {
          float gain = juce::Decibels::decibelsToGain(db, -120.0f);
          int channelsToSum = juce::jmin((int)audioBuffer.getNumChannels(),
                                         tempBuffer.getNumChannels());
          // Measure peak of the gained signal
          float blockPeak = 0.0f;
          for (int i = 0; i < channelsToSum; ++i) {
            audioBuffer.addFrom(i, 0, tempBuffer, i, 0, numSamples, gain);
            float chPeak = tempBuffer.getMagnitude(i, 0, numSamples) * gain;
            blockPeak = juce::jmax(blockPeak, chPeak);
          }
          float blockDb = juce::Decibels::gainToDecibels(blockPeak, -120.0f);
          // Bar decay: ~20 dB/sec
          float decay = 20.0f * numSamples / (float)currentSampleRate;
          float prev = peakDb.load(std::memory_order_relaxed);
          peakDb.store(juce::jmax(blockDb, prev - decay),
                       std::memory_order_relaxed);
          // Hold decay: ~5 dB/sec (slow)
          float holdDecay = 3.0f * numSamples / (float)currentSampleRate;
          float prevHold = peakHoldDb.load(std::memory_order_relaxed);
          peakHoldDb.store(juce::jmax(blockDb, prevHold - holdDecay),
                           std::memory_order_relaxed);
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
      editorWindow = std::make_unique<PluginEditorWindow>(library, editor);
    }
  }

  /// Serialize to JSON var.
  juce::var toJson() const {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", id);
    obj->setProperty("library", library);
    obj->setProperty("family", family);
    obj->setProperty("isSolo", isSolo);
    obj->setProperty("inputPort", inputPort);
    obj->setProperty("inputChannel", inputChannel);
    obj->setProperty("pluginUid", pluginUid);
    obj->setProperty("hasPlugin", pluginInstance != nullptr);
    obj->setProperty("gainDb", (double)gainDb.load(std::memory_order_relaxed));
    obj->setProperty("peakDb", (double)peakDb.load(std::memory_order_relaxed));
    obj->setProperty("peakHoldDb",
                     (double)peakHoldDb.load(std::memory_order_relaxed));
    obj->setProperty("expressionMapName",
                     expressionMap ? juce::String(expressionMap->name) : "");

    if (pluginInstance) {
      int prog = pluginInstance->getCurrentProgram();
      juce::String progName = pluginInstance->getProgramName(prog);
      obj->setProperty("programIndex", prog);
      obj->setProperty("programName", progName);
      obj->setProperty("numPrograms", pluginInstance->getNumPrograms());
    }

    return juce::var(obj);
  }
};

} // namespace fiddle
