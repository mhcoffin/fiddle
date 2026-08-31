#pragma once

#include "AnnotationRecord.h"
#include "Annotator.h"
#include "AnnotatorChain.h"
#include "ExpressionMapAnnotator.h"
#include "ExpressionMapData.h"
#include "IncomingSwitchTracker.h"
#include "LuaAnnotator.h"
#include "LuaPlugin.h"
#include "MidiCaptureLog.h"
#include "PluginEditorWindow.h"
#include "../RealtimeMpscQueue.h"
#include "../RealtimeReadGuard.h"
#include "RealtimeMidiMessage.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <filesystem>
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

  ~MixerStrip();

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
                                        float) override;
    void audioProcessorChanged(
        juce::AudioProcessor *,
        const juce::AudioProcessorListener::ChangeDetails &d) override;
  };
  PluginChangeListener pluginChangeListener_;

  juce::String id;
  juce::String library; // User-supplied VST library label (e.g. "SSP")
  juce::String family;  // Instrument family (e.g. "Strings", "Brass")
  bool isSolo = true;   // true = solo player, false = section

  // Library activation state (per-version)
  std::atomic<bool> active{true};

  // Mute/Solo state (standard mixer behavior)
  std::atomic<bool> muted{false};
  std::atomic<bool> soloed{false};

  // Input assignment (-1 = unassigned)
  std::atomic<int> inputPort{-1};
  std::atomic<int> inputChannel{-1};

  // Plugin
  int pluginUid = 0; // scanned plugin uniqueId (0 = none)
  struct PluginRuntime {
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::AudioBuffer<float> tempBuffer;
  };

  /// Message-thread alias for the currently published runtime's plugin.
  /// Ownership remains in PluginRuntime and reclamation waits for audio readers.
  juce::AudioPluginInstance *pluginInstance = nullptr;
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
  /// Automatically creates/destroys the ExpressionMapAnnotator
  /// and rebuilds the full annotator chain.
  void setExpressionMap(std::shared_ptr<ExpressionMapData> em) {
    expressionMap = std::move(em);
    if (expressionMap) {
      incomingTracker = std::make_unique<IncomingSwitchTracker>(expressionMap);
    } else {
      incomingTracker.reset();
    }
    rebuildAnnotatorChain();
  }

  // Lua plugins installed on this strip (ordered chain).
  std::vector<std::shared_ptr<LuaPlugin>> luaPlugins;

  /// Add a Lua plugin to this strip and rebuild the chain.
  void addLuaPlugin(std::shared_ptr<LuaPlugin> plugin) {
    if (plugin) {
      luaPlugins.push_back(std::move(plugin));
      rebuildAnnotatorChain();
    }
  }

  /// Remove a Lua plugin by index and rebuild the chain.
  void removeLuaPlugin(size_t index) {
    if (index < luaPlugins.size()) {
      luaPlugins.erase(luaPlugins.begin() + (ptrdiff_t)index);
      rebuildAnnotatorChain();
    }
  }

  /// Insert a Lua plugin at a specific index (for undo restore).
  void insertLuaPlugin(size_t index, std::shared_ptr<LuaPlugin> plugin) {
    if (plugin) {
      if (index >= luaPlugins.size())
        luaPlugins.push_back(std::move(plugin));
      else
        luaPlugins.insert(luaPlugins.begin() + (ptrdiff_t)index,
                          std::move(plugin));
      rebuildAnnotatorChain();
    }
  }

  /// Get just the filenames of installed Lua plugins (for serialization).
  /// Returns basenames only (e.g. "force_staccato.lua"), not full paths.
  std::vector<std::string> getLuaPluginFileNames() const {
    std::vector<std::string> names;
    for (const auto &p : luaPlugins) {
      std::filesystem::path fp(p->filePath());
      names.push_back(fp.filename().string());
    }
    return names;
  }

  /// Rebuild the annotator chain from the Lua plugins + expression map.
  /// Lua plugins run first (in order), then the ExpressionMapAnnotator.
  void rebuildAnnotatorChain() {
    auto chain = std::make_unique<AnnotatorChain>();
    for (auto &plugin : luaPlugins)
      chain->add(std::make_unique<LuaAnnotator>(plugin));
    if (expressionMap)
      chain->add(std::make_unique<ExpressionMapAnnotator>(expressionMap));
    else
      chain->add(std::make_unique<PassthroughAnnotator>());
    annotator = std::move(chain);
  }

  // Annotator chain: Lua plugins → ExpressionMapAnnotator.
  // Defaults to a chain with just a PassthroughAnnotator.
  std::unique_ptr<AnnotatorChain> annotator =
      [] {
        auto chain = std::make_unique<AnnotatorChain>();
        chain->add(std::make_unique<PassthroughAnnotator>());
        return chain;
      }();

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

  static constexpr size_t kMidiQueueCapacity = 8192;
  RealtimeMpscQueue<RealtimeMidiMessage, kMidiQueueCapacity> incomingMidi_;
  std::array<RealtimeMidiMessage, kMidiQueueCapacity> pendingMidi_{};
  size_t pendingMidiCount_ = 0; // audio thread only
  std::atomic<uint64_t> droppedMidiMessages_{0};
  std::atomic<bool> clearMidiRequested_{false};
  std::atomic<bool> panicRequested_{false};
  juce::MidiBuffer processMidiBuffer_; // audio thread only, preallocated
  std::atomic<double> currentSampleRate{44100.0};
  std::atomic<int> currentBlockSize{512};

  /// Fader gain in dB. Atomic for lock-free audio thread reads.
  /// Range: +6 dB to -120 dB. Values <= -120 treated as silence.
  std::atomic<float> gainDb{0.0f};

  /// Peak level in dB (post-fader). Lock-free, written by audio thread.
  std::atomic<float> peakDb{-120.0f};

  /// Peak-hold level in dB. Decays slowly (~5 dB/sec) for visual indicator.
  std::atomic<float> peakHoldDb{-120.0f};

  std::atomic<PluginRuntime *> activePluginRuntime_{nullptr};
  std::atomic<uint32_t> pluginReaders_{0};
  std::mutex retiredPluginMutex_;
  std::vector<std::unique_ptr<PluginRuntime>> retiredPluginRuntimes_;

  void prepareToPlay(double sampleRate, int blockSize) {
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    currentBlockSize.store(blockSize, std::memory_order_relaxed);
    processMidiBuffer_.ensureSize(128 * 1024);
    if (auto *runtime = activePluginRuntime_.load(std::memory_order_acquire)) {
      int maxChannels =
          juce::jmax(runtime->instance->getTotalNumInputChannels(),
                     runtime->instance->getTotalNumOutputChannels(), 2);
      runtime->tempBuffer.setSize(maxChannels, blockSize);
      runtime->instance->prepareToPlay(sampleRate, blockSize);
    }
  }

  void addDelayedMessage(double triggerTime, const juce::MidiMessage &msg) {
    const auto size = msg.getRawDataSize();
    if (size <= 0 || size > 3) {
      droppedMidiMessages_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    RealtimeMidiMessage queued;
    queued.triggerTimeMs = triggerTime;
    queued.size = static_cast<uint8_t>(size);
    std::copy_n(msg.getRawData(), size, queued.data);
    if (!incomingMidi_.tryPush(queued))
      droppedMidiMessages_.fetch_add(1, std::memory_order_relaxed);
  }

  /// Clear all pending delayed messages (used during graceful stop).
  void clearDelayedMessages() {
    clearMidiRequested_.store(true, std::memory_order_release);
  }

  /// Send All Notes Off + All Sound Off + Reset All Controllers to the plugin.
  /// Also clears pending delayed messages to prevent stale notes from firing.
  void allNotesOff() {
    heldKeyswitchNotes.clear();
    panicRequested_.store(true, std::memory_order_release);
  }

  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime,
                    bool anySoloed = false) {
    // Effective audibility: active AND not muted AND (no solos active OR
    // this strip soloed)
    const bool audible = active.load(std::memory_order_relaxed) &&
                         !muted.load(std::memory_order_relaxed) &&
                         (!anySoloed || soloed.load(std::memory_order_relaxed));
    processMidiBuffer_.clear();

    RealtimeMidiMessage queued;
    while (incomingMidi_.tryPop(queued)) {
      if (pendingMidiCount_ < pendingMidi_.size()) {
        pendingMidi_[pendingMidiCount_++] = queued;
      } else {
        droppedMidiMessages_.fetch_add(1, std::memory_order_relaxed);
      }
    }

    const bool panicRequested =
        panicRequested_.exchange(false, std::memory_order_acquire);
    const bool clearRequested =
        clearMidiRequested_.exchange(false, std::memory_order_acquire);
    if (panicRequested || clearRequested)
      pendingMidiCount_ = 0;

    if (panicRequested) {
      for (int channel = 1; channel <= 16; ++channel) {
        processMidiBuffer_.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
        processMidiBuffer_.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
        processMidiBuffer_.addEvent(
            juce::MidiMessage::allControllersOff(channel), 0);
      }
    }

    const int numSamples = audioBuffer.getNumSamples();
    const double sampleRate = currentSampleRate.load(std::memory_order_relaxed);
    const double blockEnd = currentTime + (1000.0 * numSamples / sampleRate);
    size_t retained = 0;
    for (size_t i = 0; i < pendingMidiCount_; ++i) {
      const auto &event = pendingMidi_[i];
      if (event.triggerTimeMs <= blockEnd) {
        const auto offset = static_cast<int>(
            std::clamp((event.triggerTimeMs - currentTime) * sampleRate / 1000.0,
                       0.0, static_cast<double>(juce::jmax(0, numSamples - 1))));
        processMidiBuffer_.addEvent(event.data, event.size, offset);
      } else {
        pendingMidi_[retained++] = event;
      }
    }
    pendingMidiCount_ = retained;

    RealtimeReadGuard<PluginRuntime> runtimeRead(activePluginRuntime_,
                                                 pluginReaders_);
    auto *runtime = runtimeRead.get();
    if (runtime) {
      int numSamples = audioBuffer.getNumSamples();

      // Safety check just in case tempBuffer isn't sized
      if (runtime->tempBuffer.getNumChannels() > 0 &&
          runtime->tempBuffer.getNumSamples() >= numSamples) {
        runtime->tempBuffer.clear();
        // Always process plugin (MIDI stays in sync even when muted)
        runtime->instance->processBlock(runtime->tempBuffer, processMidiBuffer_);

        if (!audible) {
          // Strip is muted/solo-suppressed: decay meters, don't sum audio
          float decay = 20.0f * numSamples / (float)sampleRate;
          float holdDecay = 3.0f * numSamples / (float)sampleRate;
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
            float decay = 20.0f * numSamples / (float)sampleRate;
            float holdDecay = 3.0f * numSamples / (float)sampleRate;
            float prev = peakDb.load(std::memory_order_relaxed);
            peakDb.store(juce::jmax(-120.0f, prev - decay),
                         std::memory_order_relaxed);
            float prevHold = peakHoldDb.load(std::memory_order_relaxed);
            peakHoldDb.store(juce::jmax(-120.0f, prevHold - holdDecay),
                             std::memory_order_relaxed);
          } else {
            float gain = juce::Decibels::decibelsToGain(db, -120.0f);
            int channelsToSum = juce::jmin((int)audioBuffer.getNumChannels(),
                                           runtime->tempBuffer.getNumChannels());
            // Measure peak of the gained signal
            float blockPeak = 0.0f;
            for (int i = 0; i < channelsToSum; ++i) {
              audioBuffer.addFrom(i, 0, runtime->tempBuffer, i, 0, numSamples,
                                  gain);
              float chPeak =
                  runtime->tempBuffer.getMagnitude(i, 0, numSamples) * gain;
              blockPeak = juce::jmax(blockPeak, chPeak);
            }
            float blockDb = juce::Decibels::gainToDecibels(blockPeak, -120.0f);
            // Bar decay: ~20 dB/sec
            float decay = 20.0f * numSamples / (float)sampleRate;
            float prev = peakDb.load(std::memory_order_relaxed);
            peakDb.store(juce::jmax(blockDb, prev - decay),
                         std::memory_order_relaxed);
            // Hold decay: ~5 dB/sec (slow)
            float holdDecay = 3.0f * numSamples / (float)sampleRate;
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
        desc, currentSampleRate.load(std::memory_order_relaxed),
        currentBlockSize.load(std::memory_order_relaxed),
        [this, desc, onComplete,
         weakToken](std::unique_ptr<juce::AudioPluginInstance> instance,
                    const juce::String &error) {
          // Guard against use-after-free: if the strip was destroyed while
          // the async load was in flight, abort silently.
          auto token = weakToken.lock();
          if (!token || !*token)
            return;

          if (!instance) {
            // std::cerr << "[MixerStrip " << id << "] Failed to load "
            //           << desc.name << ": " << error << std::endl;
            if (onComplete)
              onComplete(false);
            return;
          }

          // Unload the old UI if valid
          editorWindow.reset();

          int maxChannels =
              juce::jmax(instance->getTotalNumInputChannels(),
                         instance->getTotalNumOutputChannels(), 2);

          const auto sampleRate =
              currentSampleRate.load(std::memory_order_relaxed);
          const auto blockSize =
              currentBlockSize.load(std::memory_order_relaxed);
          instance->prepareToPlay(sampleRate, blockSize);

          auto runtime = std::make_unique<PluginRuntime>();
          runtime->tempBuffer.setSize(maxChannels, blockSize);
          runtime->instance = std::move(instance);

          // Register the listener before publication so every visible plugin
          // has a fully initialized runtime.
          pluginChangeListener_.stripId = id;
          pluginChangeListener_.pluginUid = desc.uniqueId;
          runtime->instance->addListener(&pluginChangeListener_);

          pluginInstance = runtime->instance.get();
          pluginUid = desc.uniqueId;
          publishPluginRuntime(runtime.release());

          // Populate initial cache (may be overwritten by setStateInformation
          // in the caller's onComplete callback)
          refreshPluginStateCache();

          // std::cerr << "[MixerStrip " << id << "] Loaded (Async): " << desc.name
          //           << std::endl;
          if (onComplete)
            onComplete(true);
        });
  }

  /// Unload the plugin and close editor.
  void unloadPlugin() {
    editorWindow.reset();
    pluginInstance = nullptr;
    publishPluginRuntime(nullptr);
    pluginUid = 0;
    cachedPluginState_.reset();
  }

  /// Destroy unpublished plugin runtimes once no audio callback can reference
  /// them. Call periodically from the message thread.
  void reclaimRetiredPluginRuntimes() {
    std::vector<std::unique_ptr<PluginRuntime>> reclaimable;
    {
      std::lock_guard<std::mutex> lock(retiredPluginMutex_);
      if (pluginReaders_.load(std::memory_order_acquire) != 0)
        return;
      reclaimable.swap(retiredPluginRuntimes_);
    }
    for (auto &runtime : reclaimable) {
      runtime->instance->removeListener(&pluginChangeListener_);
      runtime->instance->releaseResources();
    }
  }

  [[nodiscard]] uint64_t droppedMidiMessageCount() const noexcept {
    return droppedMidiMessages_.load(std::memory_order_relaxed);
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
    obj->setProperty("active", active.load(std::memory_order_relaxed));
    obj->setProperty("muted", muted.load(std::memory_order_relaxed));
    obj->setProperty("soloed", soloed.load(std::memory_order_relaxed));
    obj->setProperty("inputPort", inputPort.load(std::memory_order_relaxed));
    obj->setProperty("inputChannel",
                     inputChannel.load(std::memory_order_relaxed));
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

    // Lua plugin chain info
    {
      juce::Array<juce::var> luaArr;
      for (size_t i = 0; i < luaPlugins.size(); ++i) {
        auto *lObj = new juce::DynamicObject();
        const auto &meta = luaPlugins[i]->meta();
        lObj->setProperty("index", (int)i);
        lObj->setProperty("name", juce::String(meta.name));
        lObj->setProperty("filePath", juce::String(meta.filePath));
        lObj->setProperty("loaded", luaPlugins[i]->isLoaded());
        luaArr.add(juce::var(lObj));
      }
      obj->setProperty("luaPlugins", luaArr);
    }

    return juce::var(obj);
  }

private:
  void publishPluginRuntime(PluginRuntime *runtime) {
    std::lock_guard<std::mutex> lock(retiredPluginMutex_);
    auto *old =
        activePluginRuntime_.exchange(runtime, std::memory_order_acq_rel);
    if (old)
      retiredPluginRuntimes_.emplace_back(old);
  }
};

} // namespace fiddle
