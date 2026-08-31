#pragma once

#include "MasterInstrumentList.h"
#include "MixerStrip.h"
#include "HarmonicAnalysisService.h"
#include "../RealtimeReadGuard.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace fiddle {

/// Lightweight snapshot of strip state for undo/redo.
struct StripSnapshot {
  juce::String id, library, family;
  bool isSolo = true;
  bool muted = false;
  bool soloed = false;
  int inputPort = -1, inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityID;
  int index = 0; // position in strips_ vector
};

/// Manages an ordered list of MixerStrips. Owns a shared
/// AudioPluginFormatManager for plugin instantiation.
class MixerModel : public juce::Timer {
  struct ActiveAudioGraph {
    std::vector<MixerStrip *> strips;
  };

  using GraphReadScope = RealtimeReadGuard<ActiveAudioGraph>;

public:
  MixerModel() { 
    formatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
    commitAudioGraph();
    startTimer(100);
  }

  ~MixerModel() override {
    stopTimer();
    clear();
    auto* currentGraph = activeGraph_.exchange(nullptr);
    jassert(graphReaders_.load(std::memory_order_acquire) == 0);
    delete currentGraph;
    timerCallback();
  }

  void clear() {
    std::vector<std::unique_ptr<MixerStrip>> localStrips;
    {
      std::lock_guard<std::mutex> lock(stripsMutex_);
      localStrips.swap(strips_);
      commitAudioGraph();
    }
    
    // Transfer to garbage
    std::lock_guard<std::mutex> lock(trashMutex_);
    for (auto& s : localStrips) {
      s->unloadPlugin();
      trashStrips_.emplace_back(std::move(s));
    }
  }

  /// Add a new empty strip. Returns its ID.
  juce::String addStrip() {
    auto strip = std::make_unique<MixerStrip>();
    strip->id = juce::Uuid().toString();
    strip->library = "";

    juce::String newId = strip->id;
    std::lock_guard<std::mutex> lock(stripsMutex_);
    strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
    strips_.push_back(std::move(strip));
    commitAudioGraph();
    return newId;
  }

  /// Remove a strip by ID.
  bool removeStrip(const juce::String &id) {
    std::unique_ptr<MixerStrip> removedStrip;
    {
      std::lock_guard<std::mutex> lock(stripsMutex_);
      for (auto it = strips_.begin(); it != strips_.end(); ++it) {
        if ((*it)->id == id) {
          removedStrip = std::move(*it);
          strips_.erase(it);
          commitAudioGraph();
          break;
        }
      }
    }
    
    if (removedStrip) {
      removedStrip->unloadPlugin();
      std::lock_guard<std::mutex> lock(trashMutex_);
      trashStrips_.emplace_back(std::move(removedStrip));
      return true;
    }
    return false;
  }

  /// Remove a strip by ID, returning its ownership (for undo).
  /// Does NOT unload the plugin. Caller is responsible.
  std::unique_ptr<MixerStrip> removeStripKeepAlive(const juce::String &id) {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (auto it = strips_.begin(); it != strips_.end(); ++it) {
      if ((*it)->id == id) {
        auto strip = std::move(*it);
        strips_.erase(it);
        commitAudioGraph();
        return strip;
      }
    }
    return nullptr;
  }

  /// Insert a strip at a specific index (for undo of remove).
  void insertStripAt(std::unique_ptr<MixerStrip> strip, int index) {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
    int idx = juce::jlimit(0, (int)strips_.size(), index);
    strips_.insert(strips_.begin() + idx, std::move(strip));
    commitAudioGraph();
  }

  /// Get the index of a strip by ID (-1 if not found).
  [[nodiscard]] int stripIndex(const juce::String &id) const {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (int i = 0; i < (int)strips_.size(); ++i) {
      if (strips_[i]->id == id)
        return i;
    }
    return -1;
  }

  /// Take a snapshot of a strip's restorable state.
  [[nodiscard]] StripSnapshot snapshotStrip(const juce::String &id) const {
    StripSnapshot snap;
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (int i = 0; i < (int)strips_.size(); ++i) {
      if (strips_[i]->id == id) {
        auto &s = strips_[i];
        snap.id = s->id;
        snap.library = s->library;
        snap.family = s->family;
        snap.isSolo = s->isSolo;
        snap.muted = s->muted;
        snap.soloed = s->soloed;
        snap.inputPort = s->inputPort;
        snap.inputChannel = s->inputChannel;
        snap.pluginUid = s->pluginUid;
        snap.gainDb = s->gainDb.load(std::memory_order_relaxed);
        snap.expressionMapEntityID =
            s->expressionMap ? s->expressionMap->entityID : "";
        snap.index = i;
        break;
      }
    }
    return snap;
  }

  /// Insert a new strip right after the strip with the given ID,
  /// copying its input port/channel/family but with no plugin.
  juce::String duplicateStripAfter(const juce::String &afterId) {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    auto it = strips_.begin();
    for (; it != strips_.end(); ++it) {
      if ((*it)->id == afterId)
        break;
    }
    if (it == strips_.end())
      return {};

    auto &source = *it;
    auto strip = std::make_unique<MixerStrip>();
    strip->id = juce::Uuid().toString();
    strip->inputPort.store(source->inputPort.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    strip->inputChannel.store(
        source->inputChannel.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    strip->family = source->family;
    strip->isSolo = source->isSolo;
    strip->prepareToPlay(currentSampleRate_, currentBlockSize_);

    // Library label defaults to empty — user fills in later
    strip->library = "";

    juce::String newId = strip->id;
    strips_.insert(it + 1, std::move(strip));
    commitAudioGraph();
    return newId;
  }

  /// Find a strip by ID (nullptr if not found).
  MixerStrip *getStrip(const juce::String &id) {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (auto &s : strips_) {
      if (s->id == id)
        return s.get();
    }
    return nullptr;
  }

  /// Get raw pointers to all strips (caller must not hold them long).
  std::vector<MixerStrip *> getAllStrips() {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    std::vector<MixerStrip *> result;
    result.reserve(strips_.size());
    for (auto &s : strips_)
      result.push_back(s.get());
    return result;
  }

  /// Get the shared format manager (for plugin loading).
  juce::AudioPluginFormatManager &getFormatManager() { return formatManager_; }

  /// Get all strips count.
  [[nodiscard]] int size() const {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    return static_cast<int>(strips_.size());
  }

  /// Serialize all strips to JSON array.
  [[nodiscard]] juce::String toJson() const {
    juce::Array<juce::var> arr;
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (const auto &s : strips_)
      arr.add(s->toJson());
    return juce::JSON::toString(juce::var(arr), true);
  }

  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    // Pre-compute whether any strip is soloed
    bool anySoloed = false;
    for (auto *strip : graph->strips) {
      if (strip->soloed) {
        anySoloed = true;
        break;
      }
    }

    for (auto *strip : graph->strips) {
      strip->processBlock(audioBuffer, currentTime, anySoloed);
    }
  }

  void prepareToPlay(double sampleRate, int blockSize) {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = blockSize;
    for (auto &strip : strips_) {
      strip->prepareToPlay(sampleRate, blockSize);
    }
  }

  /// Route incoming MIDI note event to matching strips (raw, no annotation).
  void routeNoteEvent(int port, int channel, const juce::MidiMessage &msg,
                      double triggerTime) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    for (auto *strip : graph->strips) {
      if (strip->inputPort == port && strip->inputChannel == channel) {
        strip->addDelayedMessage(triggerTime, msg);
      }
    }
  }

  /// Route incoming CC event to matching strips (immediate, no delay).
  void routeCCEvent(int port, int channel, const juce::MidiMessage &msg) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    double now = juce::Time::getMillisecondCounterHiRes();
    for (auto *strip : graph->strips) {
      if (strip->inputPort == port && strip->inputChannel == channel) {
        strip->addDelayedMessage(now, msg);
      }
    }
  }

  // ── Annotator-aware routing ────────────────────────────────────────

  /// Convert a MidiInstruction proto to a juce::MidiMessage.
  /// @param noteChannel  1-based fallback channel when instruction channel==0.
  static juce::MidiMessage
  instructionToMidi(const fiddle::MidiInstruction &instr, int noteChannel) {
    int ch = instr.channel() > 0 ? instr.channel() : noteChannel;
    switch (instr.type()) {
    case fiddle::MidiInstruction::CC:
      return juce::MidiMessage::controllerEvent(ch, instr.param1(),
                                                instr.param2());
    case fiddle::MidiInstruction::KEY_SWITCH:
      return juce::MidiMessage::noteOn(ch, instr.param1(),
                                       (juce::uint8)instr.param2());
    case fiddle::MidiInstruction::PROGRAM_CHANGE:
      return juce::MidiMessage::programChange(ch, instr.param1());
    case fiddle::MidiInstruction::PITCH_BEND:
      return juce::MidiMessage::pitchWheel(ch,
                                           (instr.param1() << 7) |
                                               instr.param2());
    case fiddle::MidiInstruction::CHANNEL_PRESSURE:
      return juce::MidiMessage::channelPressureChange(ch, instr.param1());
    default:
      return juce::MidiMessage(); // empty
    }
  }

  /// Record a MidiInstruction into a capture log.
  static void captureInstruction(MidiCaptureLog &log, double timeMs,
                                 const fiddle::MidiInstruction &instr,
                                 int noteChannel,
                                 const juce::String &label = {}) {
    int ch = instr.channel() > 0 ? instr.channel() : noteChannel;
    switch (instr.type()) {
    case fiddle::MidiInstruction::CC:
      log.record(timeMs, ch, CapturedMidiEvent::CC, instr.param1(),
                 instr.param2(), label);
      break;
    case fiddle::MidiInstruction::KEY_SWITCH:
      log.record(timeMs, ch, CapturedMidiEvent::NoteOn, instr.param1(),
                 instr.param2(), label);
      break;
    case fiddle::MidiInstruction::PROGRAM_CHANGE:
      log.record(timeMs, ch, CapturedMidiEvent::ProgramChange, instr.param1(),
                 0, label);
      break;
    default:
      break;
    }
  }

  /// Route a noteOn through each matching strip's annotator, scheduling
  /// pre_note instructions before the noteOn.
  void routeAnnotatedNoteOn(int port, int channel, fiddle::Note &note,
                            double triggerTimeMs) {
    // ── Read tonal context from the harmonic analysis service ─────────────
    // The service is fed by MainComponent (onNoteEvent), so we just read.
    TonalContext tonalCtx;
    if (harmonicService_) {
      TonalContext keyCtx = harmonicService_->getContext();
      tonalCtx = HarmonicAnalysisService::annotateNote(
          keyCtx, static_cast<int>(note.note_number()));
    }

    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    for (auto *strip : graph->strips) {
      if (strip->inputPort != port || strip->inputChannel != channel)
        continue;

      // Run the annotator — it populates note.pre_note() and during_note()
      AnnotatorContext ctx;
      ctx.stripChannel = channel + 1; // convert 0-based to 1-based
      ctx.currentTimeMs = triggerTimeMs;
      ctx.sampleRate = currentSampleRate_;
      ctx.tonalContext = tonalCtx; // inject tonal context
      strip->annotator->onNoteStart(note, ctx);

      // Record the annotation decision for Note Inspector UI
      if (auto *rec = strip->annotator->lastAnnotationRecord()) {
        strip->pushAnnotation(*rec);
      }

      // ── Latching keyswitches: release old, latch new ──────────────
      // Collect the new keyswitch notes from pre_note instructions
      std::set<MixerStrip::HeldKS> newKS;
      for (const auto &instr : note.pre_note()) {
        if (instr.type() == fiddle::MidiInstruction::KEY_SWITCH) {
          int ch = instr.channel() > 0 ? instr.channel()
                                       : (int)note.channel();
          newKS.insert({ch, instr.param1()});
        }
      }

      // Schedule pre_note instructions first (new keyswitches before releasing old)
      for (const auto &instr : note.pre_note()) {
        double t = triggerTimeMs + instr.delay_ms();
        juce::MidiMessage msg =
            instructionToMidi(instr, (int)note.channel());
        strip->addDelayedMessage(t, msg);
        captureInstruction(strip->emittedCapture, t, instr,
                           (int)note.channel());
      }

      // Release keyswitches that are no longer needed (after new ones are sent)
      if (!newKS.empty()) {
        for (const auto &old : strip->heldKeyswitchNotes) {
          if (newKS.find(old) == newKS.end()) {
            double releaseTime = triggerTimeMs - 1.0;
            strip->addDelayedMessage(
                releaseTime,
                juce::MidiMessage::noteOff(old.channel, old.noteNumber,
                                           (juce::uint8)64));
            strip->emittedCapture.record(
                releaseTime, old.channel,
                CapturedMidiEvent::NoteOff, old.noteNumber, 64);
          }
        }
        strip->heldKeyswitchNotes = newKS;
      }

      // Schedule during_note instructions before noteOn (matches Dorico ordering)
      for (const auto &instr : note.during_note()) {
        double t = triggerTimeMs + instr.delay_ms();
        juce::MidiMessage msg =
            instructionToMidi(instr, (int)note.channel());
        strip->addDelayedMessage(t, msg);
        captureInstruction(strip->emittedCapture, t, instr,
                           (int)note.channel());
        // Track any keyswitches in during_note too
        if (instr.type() == fiddle::MidiInstruction::KEY_SWITCH) {
          int ch = instr.channel() > 0 ? instr.channel()
                                       : (int)note.channel();
          strip->heldKeyswitchNotes.insert({ch, instr.param1()});
        }
      }

      // Schedule the noteOn itself
      juce::MidiMessage noteOnMsg = juce::MidiMessage::noteOn(
          (int)note.channel(), (int)note.note_number(),
          (juce::uint8)note.start_velocity());
      strip->addDelayedMessage(triggerTimeMs, noteOnMsg);
      strip->emittedCapture.record(
          triggerTimeMs, (int)note.channel(),
          CapturedMidiEvent::NoteOn, (int)note.note_number(),
          (int)note.start_velocity());

      // If an annotator (e.g. Lua plugin) scheduled an early note-off,
      // schedule it now at note-on time + scheduled ms.
      double noteOffMs = strip->annotator->scheduledNoteOffMs();
      if (noteOffMs > 0) {
        double noteOffTime = triggerTimeMs + noteOffMs;
        juce::MidiMessage earlyNoteOff = juce::MidiMessage::noteOff(
            (int)note.channel(), (int)note.note_number(), (juce::uint8)64);
        strip->addDelayedMessage(noteOffTime, earlyNoteOff);
        strip->emittedCapture.record(
            noteOffTime, (int)note.channel(),
            CapturedMidiEvent::NoteOff, (int)note.note_number(), 64);
      }
    }
  }

  /// Route a noteOff through each matching strip's annotator, scheduling
  /// the noteOff followed by post_note instructions.
  void routeAnnotatedNoteOff(int port, int channel, fiddle::Note &note,
                             double triggerTimeMs) {
    // Note removal from the HMM is handled internally by the
    // HarmonicAnalysisService (via onNoteEvent called from MainComponent).
    // No action needed here.

    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    for (auto *strip : graph->strips) {
      if (strip->inputPort != port || strip->inputChannel != channel)
        continue;

      // Run the annotator — it populates note.post_note() and durationAdjustMs()
      AnnotatorContext ctx;
      ctx.stripChannel = channel + 1;
      ctx.currentTimeMs = triggerTimeMs;
      ctx.sampleRate = currentSampleRate_;
      strip->annotator->onNoteEnd(note, ctx);

      // Update the last annotation record with duration info
      if (!strip->recentAnnotations_.empty()) {
        strip->recentAnnotations_.back().durationAdjustMs =
            strip->annotator->durationAdjustMs();
      }

      // Apply duration adjustment (negative = early noteOff, positive = late)
      double adjustedTime =
          triggerTimeMs + strip->annotator->durationAdjustMs();

      // Determine the switch label from the most recent annotation
      juce::String switchLabel;
      if (!strip->recentAnnotations_.empty()) {
        const auto &baseName = strip->recentAnnotations_.back().baseSwitchName;
        if (!baseName.empty())
          switchLabel = juce::String(baseName);
      }

      // Schedule the noteOff at adjusted time (with switch label, matching Dorico)
      juce::MidiMessage noteOffMsg = juce::MidiMessage::noteOff(
          (int)note.channel(), (int)note.note_number(), (juce::uint8)64);
      strip->addDelayedMessage(adjustedTime, noteOffMsg);
      strip->emittedCapture.record(
          adjustedTime, (int)note.channel(),
          CapturedMidiEvent::NoteOff, (int)note.note_number(), 64,
          switchLabel);

      // Schedule post_note instructions (relative to adjusted noteOff time)
      for (const auto &instr : note.post_note()) {
        double t = adjustedTime + instr.delay_ms();
        juce::MidiMessage msg =
            instructionToMidi(instr, (int)note.channel());
        strip->addDelayedMessage(t, msg);
        captureInstruction(strip->emittedCapture, t, instr,
                           (int)note.channel());
        // Track any keyswitches in post_note as latched
        if (instr.type() == fiddle::MidiInstruction::KEY_SWITCH) {
          int ch = instr.channel() > 0 ? instr.channel()
                                       : (int)note.channel();
          strip->heldKeyswitchNotes.insert({ch, instr.param1()});
        }
      }
    }
  }

  /// Release all latched keyswitches on every strip (called on transport stop).
  void releaseAllKeyswitches(double triggerTimeMs) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    for (auto *strip : graph->strips) {
      for (const auto &ks : strip->heldKeyswitchNotes) {
        juce::MidiMessage noteOffMsg =
            juce::MidiMessage::noteOff(ks.channel, ks.noteNumber,
                                       (juce::uint8)64);
        strip->addDelayedMessage(triggerTimeMs, noteOffMsg);
        strip->emittedCapture.record(
            triggerTimeMs, ks.channel,
            CapturedMidiEvent::NoteOff, ks.noteNumber, 64);
      }
      strip->heldKeyswitchNotes.clear();
    }
  }

  /// Gracefully stop all sounding notes on transport stop.
  ///
  /// 1. Clears pending delayed messages (prevents stale note-ons from firing).
  /// 2. Releases latched keyswitches immediately.
  /// 3. Ramps CC1 (expression) from current value → 0 over @p releaseMs.
  /// 4. Sends note-OFF for each active note after the ramp completes.
  /// 5. Schedules All Sound Off (CC 120) shortly after to hard-kill any
  ///    lingering release tails.
  ///
  /// @param channelCC1  Map from Dorico channel (1-based) → current CC1 value.
  ///                    Channels not in the map default to 127.
  void gracefulStop(const std::vector<fiddle::Note> &activeNotes,
                    double releaseMs,
                    const std::map<int, uint8_t> &channelCC1 = {}) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph)
      return;

    releaseMs = juce::jmax(0.0, releaseMs);
    const double now = juce::Time::getMillisecondCounterHiRes();

    // 1. Clear all pending delayed messages
    for (auto *strip : graph->strips) {
      strip->clearDelayedMessages();
    }

    // 2. Release latched keyswitches immediately
    for (auto *strip : graph->strips) {
      for (const auto &ks : strip->heldKeyswitchNotes) {
        strip->addDelayedMessage(
            0.0, juce::MidiMessage::noteOff(ks.channel, ks.noteNumber,
                                            (juce::uint8)64));
      }
      strip->heldKeyswitchNotes.clear();
    }

    // 3. Ramp CC1 (expression) down to 0 over the release window.
    //    Notes stay sounding so the ramp audibly fades them out.
    constexpr int kRampSteps = 10;
    const double stepMs = releaseMs / kRampSteps;

    // Collect unique (port, channel_0based) pairs from active notes
    std::set<std::pair<int, int>> activeChannels;
    for (const auto &note : activeNotes) {
      activeChannels.insert({(int)note.port(), (int)note.channel() - 1});
    }

    for (const auto &[port, ch0] : activeChannels) {
      int ch1 = ch0 + 1; // 1-based for MIDI messages and CC lookup
      auto it = channelCC1.find(ch1);
      int startVal = (it != channelCC1.end()) ? (int)it->second : 127;
      if (startVal <= 0) continue;

      for (int i = 1; i <= kRampSteps; ++i) {
        const int val = startVal - (startVal * i / kRampSteps);
        const double t = now + (stepMs * i);
        for (auto *strip : graph->strips) {
          if (strip->inputPort == port && strip->inputChannel == ch0) {
            strip->addDelayedMessage(
                t, juce::MidiMessage::controllerEvent(ch1, 1, val));
          }
        }
      }
    }

    // 4. Send note-OFF for each active note after ramp completes
    const double noteOffTime = now + releaseMs;
    for (const auto &note : activeNotes) {
      int port = (int)note.port();
      int channel = (int)note.channel() - 1; // Dorico 1-based → 0-based
      for (auto *strip : graph->strips) {
        if (strip->inputPort == port && strip->inputChannel == channel) {
          strip->addDelayedMessage(
              noteOffTime,
              juce::MidiMessage::noteOff((int)note.channel(),
                                         (int)note.note_number(),
                                         (juce::uint8)64));
        }
      }
    }

    // 5. Schedule All Sound Off + Reset Controllers shortly after note-offs
    const double killTime = noteOffTime + 50.0;
    for (auto *strip : graph->strips) {
      for (int ch = 1; ch <= 16; ++ch) {
        strip->addDelayedMessage(killTime,
                                 juce::MidiMessage::allSoundOff(ch));
        strip->addDelayedMessage(killTime,
                                 juce::MidiMessage::allControllersOff(ch));
      }
    }
  }

  /// Route a CC event through matching strip annotators.
  /// If the annotator's onCC returns false, the CC is not forwarded.
  void routeAnnotatedCC(int port, int channel, const fiddle::MidiEvent &event,
                        const juce::MidiMessage &msg, double triggerTimeMs) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    for (auto *strip : graph->strips) {
      if (strip->inputPort != port || strip->inputChannel != channel)
        continue;

      AnnotatorContext ctx;
      ctx.stripChannel = channel + 1;
      ctx.currentTimeMs = triggerTimeMs;

      if (strip->annotator->onCC(event, ctx)) {
        strip->addDelayedMessage(triggerTimeMs, msg);
        if (event.has_cc()) {
          strip->emittedCapture.record(
              triggerTimeMs, channel + 1, CapturedMidiEvent::CC,
              (int)event.cc().controller_number(),
              (int)event.cc().controller_value());
        }
      }
    }
  }

  /// Record raw incoming MIDI from Dorico to matching strips.
  void recordIncomingMidi(int port, int channel,
                          CapturedMidiEvent::Type type, int p1, int p2,
                          double timeMs) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    for (auto *strip : graph->strips) {
      if (strip->inputPort != port || strip->inputChannel != channel)
        continue;

      // Feed event to the incoming switch tracker (if present)
      if (strip->incomingTracker)
        strip->incomingTracker->onMidi(type, p1, p2, timeMs);

      // For NoteOff of real notes, resolve matching switch names
      juce::String label;
      if (type == CapturedMidiEvent::NoteOff && strip->incomingTracker &&
          !strip->incomingTracker->isKeyswitch(p1)) {
        auto names = strip->incomingTracker->resolveNoteOff(p1, timeMs);
        for (size_t i = 0; i < names.size(); ++i) {
          if (i > 0)
            label += "|";
          label += juce::String(names[i]);
        }
      }

      strip->incomingCapture.record(timeMs, channel + 1, type, p1, p2, label);
    }
  }

  /// Panic: send All Notes Off to every strip.
  void allNotesOff() {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph) return;

    for (auto *strip : graph->strips) {
      strip->allNotesOff();
    }
  }

  /// Panic: send All Notes Off only to strips matching port and channel.
  void panicStrips(int port, int channel) {
    GraphReadScope graphRead(activeGraph_, graphReaders_);
    auto *graph = graphRead.get();
    if (!graph)
      return;

    for (auto *strip : graph->strips) {
      if (strip->inputPort == port && strip->inputChannel == channel) {
        strip->allNotesOff();
      }
    }
  }

  /// Set mute state on a strip by ID.
  bool setStripMute(const juce::String &id, bool mute) {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (auto &s : strips_) {
      if (s->id == id) {
        s->muted = mute;
        return true;
      }
    }
    return false;
  }

  /// Set solo state on a strip by ID.
  bool setStripSolo(const juce::String &id, bool solo) {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (auto &s : strips_) {
      if (s->id == id) {
        s->soloed = solo;
        return true;
      }
    }
    return false;
  }

  /// Sync mixer strips to match the ensemble instrument list.
  /// Creates new strips for new instruments, removes strips whose
  /// port/channel no longer appears. Preserves existing plugin assignments.
  void syncStripsToInstruments(const MasterInstrumentList &masterList) {
    // Build expected set from the channel map, which uses persistent
    // assignments for port/channel (not flat-index recomputation).
    struct Entry {
      int port;
      int channel;
      juce::String label;
      juce::String family;
      bool isSolo;
    };
    std::vector<Entry> expected;
    std::set<std::pair<int, int>> expectedSet;

    juce::String mapJson = masterList.getChannelMapAsJson();
    auto parsed = juce::JSON::parse(mapJson);
    if (auto *arr = parsed.getArray()) {
      for (const auto &item : *arr) {
        if (auto *obj = item.getDynamicObject()) {
          int port = (int)obj->getProperty("port");
          int ch = (int)obj->getProperty("channel");

          // Skip the reserved metronome channel (port 0, channel 0).
          // It is a tempo-analysis signal, not an audio instrument.
          if (port == 0 && ch == 0) continue;

          juce::String label = obj->getProperty("label").toString();
          juce::String family = obj->getProperty("family").toString();
          bool solo = (bool)obj->getProperty("isSolo");
          expected.push_back({port, ch, label, family, solo});
          expectedSet.insert({port, ch});
        }
      }
    }

    std::unique_lock<std::mutex> lock(stripsMutex_);
    std::vector<std::unique_ptr<MixerStrip>> removedStrips;

    // Remove strips whose port/channel is no longer in the expected set
    for (auto it = strips_.begin(); it != strips_.end();) {
      auto key = std::make_pair(
          (*it)->inputPort.load(std::memory_order_relaxed),
          (*it)->inputChannel.load(std::memory_order_relaxed));
      if (expectedSet.find(key) == expectedSet.end()) {
        removedStrips.push_back(std::move(*it));
        it = strips_.erase(it);
      } else {
        ++it;
      }
    }
    // Build a lookup from port/channel to expected entry
    std::map<std::pair<int, int>, const Entry *> expectedMap;
    for (const auto &e : expected)
      expectedMap[{e.port, e.channel}] = &e;

    // Update family/name on existing strips and build existing set
    std::set<std::pair<int, int>> existingSet;
    for (auto &s : strips_) {
      auto key = std::make_pair(s->inputPort.load(std::memory_order_relaxed),
                                s->inputChannel.load(std::memory_order_relaxed));
      existingSet.insert(key);
      auto it2 = expectedMap.find(key);
      if (it2 != expectedMap.end()) {
        s->family = it2->second->family;
        s->isSolo = it2->second->isSolo;
      }
    }

    // Add strips for new instruments
    for (const auto &entry : expected) {
      auto key = std::make_pair(entry.port, entry.channel);
      if (existingSet.find(key) == existingSet.end()) {
        auto strip = std::make_unique<MixerStrip>();
        strip->id = juce::Uuid().toString();
        strip->library = "";
        strip->family = entry.family;
        strip->isSolo = entry.isSolo;
        strip->inputPort = entry.port;
        strip->inputChannel = entry.channel;
        strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
        strips_.push_back(std::move(strip));
      }
    }
    commitAudioGraph();
    lock.unlock();

    if (!removedStrips.empty()) {
      for (auto &strip : removedStrips)
        strip->unloadPlugin();
      std::lock_guard<std::mutex> trashLock(trashMutex_);
      for (auto &strip : removedStrips)
        trashStrips_.push_back(std::move(strip));
    }
  }

  /// Set the harmonic analysis service (owned by MainComponent, not MixerModel).
  void setHarmonicService(HarmonicAnalysisService *service) {
    harmonicService_ = service;
  }

private:
  // Only the main thread accesses these UI representations
  mutable std::mutex stripsMutex_;
  std::vector<std::unique_ptr<MixerStrip>> strips_;

  std::atomic<ActiveAudioGraph*> activeGraph_{nullptr};
  std::atomic<uint32_t> graphReaders_{0};

  // Garbage bin emptied by timer
  mutable std::mutex trashMutex_;
  std::vector<std::unique_ptr<ActiveAudioGraph>> trashGraphs_;
  std::vector<std::unique_ptr<MixerStrip>> trashStrips_;

  void commitAudioGraph() {
    auto* current = new ActiveAudioGraph();
    for (auto& s : strips_) {
      current->strips.push_back(s.get());
    }
    std::lock_guard<std::mutex> lock(trashMutex_);
    auto* old = activeGraph_.exchange(current, std::memory_order_acq_rel);
    if (old)
      trashGraphs_.emplace_back(old);
  }

  void timerCallback() override {
    std::vector<std::unique_ptr<ActiveAudioGraph>> graphsToDelete;
    std::vector<std::unique_ptr<MixerStrip>> stripsToDelete;

    {
      std::lock_guard<std::mutex> lock(trashMutex_);
      if (graphReaders_.load(std::memory_order_acquire) == 0) {
        graphsToDelete.swap(trashGraphs_);
        stripsToDelete.swap(trashStrips_);
      }
    }

    // Reclamation always happens on the message thread, never in processBlock.
    {
      std::lock_guard<std::mutex> lock(stripsMutex_);
      for (auto &strip : strips_)
        strip->reclaimRetiredPluginRuntimes();
    }
    graphsToDelete.clear();
    stripsToDelete.clear();
  }
  juce::AudioPluginFormatManager formatManager_;
  int nextStripNumber_ = 1;
  double currentSampleRate_ = 44100.0;
  int currentBlockSize_ = 512;
  int playbackDelayMs_ = 1000;

  // Harmonic analysis service (optional). Owned by MainComponent.
  HarmonicAnalysisService *harmonicService_ = nullptr;

public:
  int getPlaybackDelayMs() const { return playbackDelayMs_; }
  void setPlaybackDelayMs(int ms) { playbackDelayMs_ = ms; }
};

} // namespace fiddle
