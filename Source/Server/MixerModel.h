#pragma once

#include "MasterInstrumentList.h"
#include "MixerStrip.h"
#include <juce_audio_processors/juce_audio_processors.h>
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
class MixerModel {
public:
  MixerModel() { formatManager_.addFormat(new juce::VST3PluginFormat()); }

  ~MixerModel() { clear(); }

  void clear() {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
      strip->unloadPlugin();
    }
    strips_.clear();
  }

  /// Add a new empty strip. Returns its ID.
  juce::String addStrip() {
    auto strip = std::make_unique<MixerStrip>();
    strip->id = juce::Uuid().toString();
    strip->library = "";

    std::lock_guard<std::mutex> lock(stripsMutex);
    strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
    strips_.push_back(std::move(strip));
    return strips_.back()->id;
  }

  /// Remove a strip by ID.
  bool removeStrip(const juce::String &id) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto it = strips_.begin(); it != strips_.end(); ++it) {
      if ((*it)->id == id) {
        (*it)->unloadPlugin();
        strips_.erase(it);
        return true;
      }
    }
    return false;
  }

  /// Remove a strip by ID, returning its ownership (for undo).
  /// Does NOT unload the plugin. Caller is responsible.
  std::unique_ptr<MixerStrip> removeStripKeepAlive(const juce::String &id) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto it = strips_.begin(); it != strips_.end(); ++it) {
      if ((*it)->id == id) {
        auto strip = std::move(*it);
        strips_.erase(it);
        return strip;
      }
    }
    return nullptr;
  }

  /// Insert a strip at a specific index (for undo of remove).
  void insertStripAt(std::unique_ptr<MixerStrip> strip, int index) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
    int idx = juce::jlimit(0, (int)strips_.size(), index);
    strips_.insert(strips_.begin() + idx, std::move(strip));
  }

  /// Get the index of a strip by ID (-1 if not found).
  int stripIndex(const juce::String &id) const {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (int i = 0; i < (int)strips_.size(); ++i) {
      if (strips_[i]->id == id)
        return i;
    }
    return -1;
  }

  /// Take a snapshot of a strip's restorable state.
  StripSnapshot snapshotStrip(const juce::String &id) {
    StripSnapshot snap;
    std::lock_guard<std::mutex> lock(stripsMutex);
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
    std::lock_guard<std::mutex> lock(stripsMutex);
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
    strip->inputPort = source->inputPort;
    strip->inputChannel = source->inputChannel;
    strip->family = source->family;
    strip->isSolo = source->isSolo;
    strip->prepareToPlay(currentSampleRate_, currentBlockSize_);

    // Library label defaults to empty — user fills in later
    strip->library = "";

    juce::String newId = strip->id;
    strips_.insert(it + 1, std::move(strip));
    return newId;
  }

  /// Find a strip by ID (nullptr if not found).
  MixerStrip *getStrip(const juce::String &id) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &s : strips_) {
      if (s->id == id)
        return s.get();
    }
    return nullptr;
  }

  /// Get raw pointers to all strips (caller must not hold them long).
  std::vector<MixerStrip *> getAllStrips() {
    std::lock_guard<std::mutex> lock(stripsMutex);
    std::vector<MixerStrip *> result;
    result.reserve(strips_.size());
    for (auto &s : strips_)
      result.push_back(s.get());
    return result;
  }

  /// Get the shared format manager (for plugin loading).
  juce::AudioPluginFormatManager &getFormatManager() { return formatManager_; }

  /// Get all strips count.
  int size() const {
    std::lock_guard<std::mutex> lock(stripsMutex);
    return static_cast<int>(strips_.size());
  }

  /// Serialize all strips to JSON array.
  juce::String toJson() const {
    juce::Array<juce::var> arr;
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (const auto &s : strips_)
      arr.add(s->toJson());
    return juce::JSON::toString(juce::var(arr), true);
  }

  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime) {
    std::lock_guard<std::mutex> lock(stripsMutex);

    // Pre-compute whether any strip is soloed
    bool anySoloed = false;
    for (auto &strip : strips_) {
      if (strip->soloed) {
        anySoloed = true;
        break;
      }
    }

    for (auto &strip : strips_) {
      strip->processBlock(audioBuffer, currentTime, anySoloed);
    }
  }

  void prepareToPlay(double sampleRate, int blockSize) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = blockSize;
    for (auto &strip : strips_) {
      strip->prepareToPlay(sampleRate, blockSize);
    }
  }

  /// Route incoming MIDI note event to matching strips (raw, no annotation).
  void routeNoteEvent(int port, int channel, const juce::MidiMessage &msg,
                      double triggerTime) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
      if (strip->inputPort == port && strip->inputChannel == channel) {
        strip->addDelayedMessage(triggerTime, msg);
      }
    }
  }

  /// Route incoming CC event to matching strips (immediate, no delay).
  void routeCCEvent(int port, int channel, const juce::MidiMessage &msg) {
    double now = juce::Time::getMillisecondCounterHiRes();
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
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
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
      if (strip->inputPort != port || strip->inputChannel != channel)
        continue;

      // Run the annotator — it populates note.pre_note() and during_note()
      AnnotatorContext ctx;
      ctx.stripChannel = channel + 1; // convert 0-based to 1-based
      ctx.currentTimeMs = triggerTimeMs;
      ctx.sampleRate = currentSampleRate_;
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
    }
  }

  /// Route a noteOff through each matching strip's annotator, scheduling
  /// the noteOff followed by post_note instructions.
  void routeAnnotatedNoteOff(int port, int channel, fiddle::Note &note,
                             double triggerTimeMs) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
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
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
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

  /// Route a CC event through matching strip annotators.
  /// If the annotator's onCC returns false, the CC is not forwarded.
  void routeAnnotatedCC(int port, int channel, const fiddle::MidiEvent &event,
                        const juce::MidiMessage &msg, double triggerTimeMs) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
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
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
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
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
      strip->allNotesOff();
    }
  }

  /// Set mute state on a strip by ID.
  bool setStripMute(const juce::String &id, bool mute) {
    std::lock_guard<std::mutex> lock(stripsMutex);
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
    std::lock_guard<std::mutex> lock(stripsMutex);
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
          juce::String label = obj->getProperty("label").toString();
          juce::String family = obj->getProperty("family").toString();
          bool solo = (bool)obj->getProperty("isSolo");
          expected.push_back({port, ch, label, family, solo});
          expectedSet.insert({port, ch});
        }
      }
    }

    std::lock_guard<std::mutex> lock(stripsMutex);

    // Remove strips whose port/channel is no longer in the expected set
    for (auto it = strips_.begin(); it != strips_.end();) {
      auto key = std::make_pair((*it)->inputPort, (*it)->inputChannel);
      if (expectedSet.find(key) == expectedSet.end()) {
        (*it)->unloadPlugin();
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
      auto key = std::make_pair(s->inputPort, s->inputChannel);
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
  }

private:
  mutable std::mutex stripsMutex;
  std::vector<std::unique_ptr<MixerStrip>> strips_;
  juce::AudioPluginFormatManager formatManager_;
  int nextStripNumber_ = 1;
  double currentSampleRate_ = 44100.0;
  int currentBlockSize_ = 512;
  int playbackDelayMs_ = 1000;

public:
  int getPlaybackDelayMs() const { return playbackDelayMs_; }
  void setPlaybackDelayMs(int ms) { playbackDelayMs_ = ms; }
};

} // namespace fiddle
