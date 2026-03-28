#pragma once

#include "AnnotationRecord.h"
#include "Annotator.h"
#include "ExpressionMapAnnotator.h"
#include "ExpressionMapData.h"
#include "IncomingSwitchTracker.h"
#include "MidiCaptureLog.h"
#include "PluginEditorWindow.h"
#include <atomic>
#include <deque>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace fiddle {

/// A single mixer channel strip. Owns a plugin instance + optional editor
/// window. Identified by a unique string ID.
struct MixerStrip {

  /// Lifetime sentinel: shared_ptr set to false when the strip is destroyed.
  /// The async loadPlugin callback captures a weak_ptr to this and checks
  /// liveness before touching any member — prevents use-after-free when a
  /// strip is removed from the mixer while a plugin load is still in flight.
  std::shared_ptr<bool> lifetimeToken_ = std::make_shared<bool>(true);

  ~MixerStrip() {
    if (lifetimeToken_)
      *lifetimeToken_ = false;
  }

  /// Listener that detects plugin parameter changes via the standard VST3
  /// notification path. When it fires, we mark the strip dirty and record
  /// the pluginUid as "listener-capable" so polling can skip it.
  struct PluginChangeListener : juce::AudioProcessorListener {
    juce::String stripId;
    int pluginUid = 0;
    /// Shared set of pluginUids known to fire listener callbacks.
    /// Populated by the listener, read by the polling timer.
    std::set<int> *listenerCapableUids = nullptr;
    /// Callback to mark Fiddle dirty (set by MainComponent).
    std::function<void()> onDirty;

    void audioProcessorParameterChanged(juce::AudioProcessor *, int,
                                        float) override {
      if (listenerCapableUids && pluginUid != 0)
        listenerCapableUids->insert(pluginUid);
      if (onDirty)
        onDirty();
    }
    void audioProcessorChanged(
        juce::AudioProcessor *,
        const juce::AudioProcessorListener::ChangeDetails &d) override {
      if (d.nonParameterStateChanged) {
        if (listenerCapableUids && pluginUid != 0)
          listenerCapableUids->insert(pluginUid);
        if (onDirty)
          onDirty();
      }
    }
  };
  PluginChangeListener pluginChangeListener_;

  juce::String id;
  juce::String library; // User-supplied VST library label (e.g. "SSP")
  juce::String family;  // Instrument family (e.g. "Strings", "Brass")
  bool isSolo = true;   // true = solo player, false = section

  // Mute/Solo state (standard mixer behavior)
  bool muted = false;
  bool soloed = false;

  // Input assignment (-1 = unassigned)
  int inputPort = -1;
  int inputChannel = -1;

  // Plugin
  int pluginUid = 0; // scanned plugin uniqueId (0 = none)
  std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
  std::unique_ptr<PluginEditorWindow> editorWindow;

  /// Cached serialized plugin state. Updated on load, restore, and change
  /// detection. Used by buildStateBlob() to avoid calling getStateInformation()
  /// on every plugin for every blob rebuild.
  juce::MemoryBlock cachedPluginState_;

  /// Re-serialize the live plugin state into cachedPluginState_.
  void refreshPluginStateCache() {
    if (pluginInstance) {
      cachedPluginState_.reset();
      pluginInstance->getStateInformation(cachedPluginState_);
    } else {
      cachedPluginState_.reset();
    }
  }

  // Expression map
  std::shared_ptr<ExpressionMapData> expressionMap;
  juce::String expressionMapPath; // file path for persistence

  /// Set (or clear) the expression map for this strip.
  /// Automatically creates/destroys the ExpressionMapAnnotator.
  void setExpressionMap(std::shared_ptr<ExpressionMapData> em) {
    expressionMap = std::move(em);
    if (expressionMap) {
      annotator = std::make_unique<ExpressionMapAnnotator>(expressionMap);
      incomingTracker = std::make_unique<IncomingSwitchTracker>(expressionMap);
    } else {
      annotator = std::make_unique<PassthroughAnnotator>();
      incomingTracker.reset();
    }
  }

  // Annotator: translates semantic note info into concrete MIDI for this VST.
  // Defaults to PassthroughAnnotator (no transformation).
  std::unique_ptr<Annotator> annotator =
      std::make_unique<PassthroughAnnotator>();

  // Incoming switch tracker: reverse-matches Dorico MIDI to switch names.
  std::unique_ptr<IncomingSwitchTracker> incomingTracker;

  // Ring buffer of recent annotation decision records for Note Inspector UI.
  std::deque<AnnotationRecord> recentAnnotations_;
  static constexpr size_t kMaxAnnotations = 64;

  void pushAnnotation(const AnnotationRecord &rec) {
    recentAnnotations_.push_back(rec);
    while (recentAnnotations_.size() > kMaxAnnotations)
      recentAnnotations_.pop_front();
  }

  void clearAnnotations() {
    recentAnnotations_.clear();
    if (annotator)
      annotator->resetState();
  }

  // MIDI capture logs for expression map comparison.
  // incomingCapture: raw MIDI from Dorico (ground truth when EM is in Dorico).
  // emittedCapture:  MIDI emitted to the hosted VST by FiddleServer.
  MidiCaptureLog incomingCapture;
  MidiCaptureLog emittedCapture;

  /// Currently held (latching) keyswitch notes per channel.
  /// Key: {channel, noteNumber}. Keyswitches are held until articulation
  /// changes, matching Dorico's latching behavior for VSTs.
  struct HeldKS {
    int channel, noteNumber;
    bool operator<(const HeldKS &o) const {
      return std::tie(channel, noteNumber) < std::tie(o.channel, o.noteNumber);
    }
    bool operator==(const HeldKS &o) const {
      return channel == o.channel && noteNumber == o.noteNumber;
    }
  };
  std::set<HeldKS> heldKeyswitchNotes;

  /// Serialize one AnnotationRecord to a juce::var (DynamicObject).
  static juce::var annotationRecordToVar(const AnnotationRecord &r) {
    auto *obj = new juce::DynamicObject();

    obj->setProperty("noteNumber", r.noteNumber);

    // Input techniques
    {
      juce::Array<juce::var> arr;
      for (const auto &t : r.inputTechniques)
        arr.add(juce::String(t));
      obj->setProperty("inputTechniques", juce::var(arr));
    }

    // Match resolution
    obj->setProperty("lengthCategory", static_cast<int>(r.lengthCategory));
    obj->setProperty("baseSwitchName", juce::String(r.baseSwitchName));
    {
      juce::Array<juce::var> arr;
      for (const auto &t : r.baseTechniqueIDs)
        arr.add(juce::String(t));
      obj->setProperty("baseTechniqueIDs", juce::var(arr));
    }
    {
      juce::Array<juce::var> arr;
      for (const auto &n : r.addOnNames)
        arr.add(juce::String(n));
      obj->setProperty("addOnNames", juce::var(arr));
    }
    {
      juce::Array<juce::var> arr;
      for (const auto &t : r.unmatchedTechniques)
        arr.add(juce::String(t));
      obj->setProperty("unmatchedTechniques", juce::var(arr));
    }

    // Candidates (Tier 3)
    if (!r.candidates.empty()) {
      juce::Array<juce::var> arr;
      for (const auto &c : r.candidates) {
        auto *cObj = new juce::DynamicObject();
        cObj->setProperty("name", juce::String(c.name));
        cObj->setProperty("score", c.score);
        cObj->setProperty("isSubset", c.isSubset);
        if (!c.conditionString.empty())
          cObj->setProperty("condition", juce::String(c.conditionString));
        juce::Array<juce::var> tArr;
        for (const auto &t : c.techniqueIDs)
          tArr.add(juce::String(t));
        cObj->setProperty("techniqueIDs", juce::var(tArr));
        arr.add(juce::var(cObj));
      }
      obj->setProperty("candidates", juce::var(arr));
    }

    // MIDI actions
    {
      juce::Array<juce::var> arr;
      for (const auto &a : r.midiActionsEmitted) {
        auto *aObj = new juce::DynamicObject();
        aObj->setProperty("type", static_cast<int>(a.type));
        aObj->setProperty("param1", a.param1);
        aObj->setProperty("param2", a.param2);
        arr.add(juce::var(aObj));
      }
      obj->setProperty("midiActionsEmitted", juce::var(arr));
    }
    obj->setProperty("redundancySkipped", r.redundancySkipped);

    // Modifiers
    obj->setProperty("velocityBefore", r.velocityBefore);
    obj->setProperty("velocityAfter", r.velocityAfter);
    obj->setProperty("transposeApplied", r.transposeApplied);

    // Dynamics + duration
    obj->setProperty("dynamicsRouting", juce::String(r.dynamicsRouting));
    obj->setProperty("durationAdjustMs", r.durationAdjustMs);

    return juce::var(obj);
  }

  /// Serialize the entire ring buffer to a JSON string.
  juce::String getAnnotationRecordsAsJson() const {
    juce::Array<juce::var> arr;
    for (const auto &rec : recentAnnotations_)
      arr.add(annotationRecordToVar(rec));
    return juce::JSON::toString(juce::var(arr), true);
  }


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
    heldKeyswitchNotes.clear();
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

  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime,
                    bool anySoloed = false) {
    // Effective audibility: not muted AND (no solos active OR this strip
    // soloed)
    bool audible = !muted && (!anySoloed || soloed);
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
        // Always process plugin (MIDI stays in sync even when muted)
        pluginInstance->processBlock(tempBuffer, midiBuffer);

        if (!audible) {
          // Strip is muted/solo-suppressed: decay meters, don't sum audio
          float decay = 20.0f * numSamples / (float)currentSampleRate;
          float holdDecay = 3.0f * numSamples / (float)currentSampleRate;
          float prev = peakDb.load(std::memory_order_relaxed);
          peakDb.store(juce::jmax(-120.0f, prev - decay),
                       std::memory_order_relaxed);
          float prevHold = peakHoldDb.load(std::memory_order_relaxed);
          peakHoldDb.store(juce::jmax(-120.0f, prevHold - holdDecay),
                           std::memory_order_relaxed);
        } else {
          // Apply fader gain and mix down to the main host buffer
          float db = gainDb.load(std::memory_order_relaxed);
          if (db <= -120.0f) {
            // Fader at silence — decay peak
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
  }

  /// Load a plugin from a description. Must be called on the message thread.
  void loadPlugin(const juce::PluginDescription &desc,
                  juce::AudioPluginFormatManager &formatManager,
                  std::function<void(bool)> onComplete = nullptr) {

    std::weak_ptr<bool> weakToken = lifetimeToken_;
    formatManager.createPluginInstanceAsync(
        desc, currentSampleRate, currentBlockSize,
        [this, desc, onComplete,
         weakToken](std::unique_ptr<juce::AudioPluginInstance> instance,
                    const juce::String &error) {
          // Guard against use-after-free: if the strip was destroyed while
          // the async load was in flight, abort silently.
          auto token = weakToken.lock();
          if (!token || !*token)
            return;

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

            // Remove listener from old plugin before swapping
            if (pluginInstance)
              pluginInstance->removeListener(&pluginChangeListener_);

            oldPlugin = std::move(pluginInstance);
            pluginInstance = std::move(instance);
            pluginUid = desc.uniqueId;
          }

          // Register change listener on the new plugin
          pluginChangeListener_.stripId = id;
          pluginChangeListener_.pluginUid = pluginUid;
          pluginInstance->addListener(&pluginChangeListener_);

          // Populate initial cache (may be overwritten by setStateInformation
          // in the caller's onComplete callback)
          refreshPluginStateCache();

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
      pluginInstance->removeListener(&pluginChangeListener_);
      pluginInstance->releaseResources();
      pluginInstance.reset();
    }
    pluginUid = 0;
    cachedPluginState_.reset();
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
    obj->setProperty("muted", muted);
    obj->setProperty("soloed", soloed);
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
    obj->setProperty("expressionMapEntityID",
                     expressionMap ? juce::String(expressionMap->entityID)
                                   : "");

    if (pluginInstance) {
      int prog = pluginInstance->getCurrentProgram();
      int numProgs = pluginInstance->getNumPrograms();
      juce::String progName = pluginInstance->getProgramName(prog);
      obj->setProperty("programIndex", prog);
      obj->setProperty("programName", progName);
      obj->setProperty("numPrograms", numProgs);

      // Emit all program names so the UI can show a dropdown.
      juce::Array<juce::var> names;
      for (int i = 0; i < numProgs; ++i)
        names.add(pluginInstance->getProgramName(i));
      obj->setProperty("programNames", names);
    }

    return juce::var(obj);
  }
};

} // namespace fiddle
