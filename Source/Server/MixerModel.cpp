#include "MixerModel.h"
#include "NoteInspection.h"

#include "HarmonicAnalysisService.h"
#include "MasterInstrumentList.h"

namespace fiddle {

MixerModel::MixerModel() {
  formatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
  commitAudioGraph();
  startTimer(100);
}

MixerModel::~MixerModel() {
  stopTimer();
  clear();
  audioGraph_.publish(nullptr);
  jassert(audioGraph_.readerCount() == 0);
  timerCallback();
}

void MixerModel::clear() {
  std::vector<std::shared_ptr<MixerStrip>> localStrips;
  std::vector<std::shared_ptr<GroupBus>> localBuses;
  {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    localStrips.swap(strips_);
    localBuses.swap(groupBuses_);
    commitAudioGraph();
  }

  // Transfer to garbage
  std::lock_guard<std::mutex> lock(retiredStripMutex_);
  for (auto &s : localStrips) {
    s->unloadPlugin();
    trashStrips_.emplace_back(std::move(s));
  }
  for (auto &bus : localBuses)
    trashGroupBuses_.emplace_back(std::move(bus));
}

juce::String MixerModel::addStrip() {
  auto strip = std::make_unique<MixerStrip>();
  strip->id = juce::Uuid().toString();
  strip->updatePluginSlotId();
  strip->library = "";

  juce::String newId = strip->id;
  std::lock_guard<std::mutex> lock(stripsMutex_);
  strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
  strips_.push_back(std::move(strip));
  commitAudioGraph();
  return newId;
}

bool MixerModel::removeStrip(const juce::String &id) {
  std::shared_ptr<MixerStrip> removedStrip;
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
    std::lock_guard<std::mutex> lock(retiredStripMutex_);
    trashStrips_.emplace_back(std::move(removedStrip));
    return true;
  }
  return false;
}

std::shared_ptr<MixerStrip>
MixerModel::removeStripKeepAlive(const juce::String &id) {
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

void MixerModel::insertStripAt(std::shared_ptr<MixerStrip> strip, int index) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  strip->updatePluginSlotId();
  strip->prepareIfNeeded(currentSampleRate_, currentBlockSize_);
  int idx = juce::jlimit(0, (int)strips_.size(), index);
  strips_.insert(strips_.begin() + idx, std::move(strip));
  commitAudioGraph();
}

int MixerModel::stripIndex(const juce::String &id) const {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (int i = 0; i < (int)strips_.size(); ++i) {
    if (strips_[i]->id == id)
      return i;
  }
  return -1;
}

StripSnapshot MixerModel::snapshotStrip(const juce::String &id) const {
  StripSnapshot snap;
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (int i = 0; i < (int)strips_.size(); ++i) {
    if (strips_[i]->id == id) {
      auto &s = strips_[i];
      snap.id = s->id;
      snap.library = s->library;
      snap.family = s->family;
      snap.isSolo = s->isSolo;
      const auto state = s->realtimeState();
      snap.muted = state.muted;
      snap.soloed = state.soloed;
      snap.inputPort = state.inputPort;
      snap.inputChannel = state.inputChannel;
      snap.pluginUid = s->pluginUid;
      snap.gainDb = state.gainDb;
      snap.expressionMapEntityID =
          s->expressionMap ? s->expressionMap->entityID : "";
      snap.directOutputBusId = s->directOutputBusId;
      snap.index = i;
      break;
    }
  }
  return snap;
}

juce::String MixerModel::duplicateStripAfter(const juce::String &afterId) {
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
  strip->updatePluginSlotId();
  const auto sourceState = source->realtimeState();
  strip->setInputAssignment(sourceState.inputPort, sourceState.inputChannel);
  strip->family = source->family;
  strip->isSolo = source->isSolo;
  strip->directOutputBusId = source->directOutputBusId;
  strip->prepareToPlay(currentSampleRate_, currentBlockSize_);

  // Library label defaults to empty — user fills in later
  strip->library = "";

  juce::String newId = strip->id;
  strips_.insert(it + 1, std::move(strip));
  commitAudioGraph();
  return newId;
}

MixerStrip *MixerModel::getStrip(const juce::String &id) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (auto &s : strips_) {
    if (s->id == id)
      return s.get();
  }
  return nullptr;
}

std::vector<MixerStrip *> MixerModel::getAllStrips() {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  std::vector<MixerStrip *> result;
  result.reserve(strips_.size());
  for (auto &s : strips_)
    result.push_back(s.get());
  return result;
}

juce::String MixerModel::addGroupBus(const juce::String &name) {
  auto bus = std::make_unique<GroupBus>();
  bus->id = juce::Uuid().toString();
  bus->name = name.trim().isNotEmpty() ? name.trim() : "Group";
  bus->prepareIfNeeded(currentSampleRate_, currentBlockSize_);
  const auto id = bus->id;
  std::lock_guard<std::mutex> lock(stripsMutex_);
  groupBuses_.push_back(std::move(bus));
  commitAudioGraph();
  return id;
}

void MixerModel::insertGroupBusAt(std::shared_ptr<GroupBus> bus, int index) {
  if (!bus || bus->id.isEmpty())
    return;
  bus->prepareIfNeeded(currentSampleRate_, currentBlockSize_);
  std::lock_guard<std::mutex> lock(stripsMutex_);
  index = juce::jlimit(0, static_cast<int>(groupBuses_.size()), index);
  groupBuses_.insert(groupBuses_.begin() + index, std::move(bus));
  commitAudioGraph();
}

bool MixerModel::removeGroupBus(const juce::String &id) {
  auto removed = removeGroupBusKeepAlive(id);
  if (!removed)
    return false;
  std::lock_guard<std::mutex> lock(retiredStripMutex_);
  trashGroupBuses_.push_back(std::move(removed));
  return true;
}

std::shared_ptr<GroupBus>
MixerModel::removeGroupBusKeepAlive(const juce::String &id) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  const auto found = std::find_if(
      groupBuses_.begin(), groupBuses_.end(),
      [&id](const auto &bus) { return bus->id == id; });
  if (found == groupBuses_.end())
    return nullptr;
  for (auto &strip : strips_)
    if (strip->directOutputBusId == id)
      strip->directOutputBusId.clear();
  auto removed = std::move(*found);
  groupBuses_.erase(found);
  commitAudioGraph();
  return removed;
}

int MixerModel::groupBusIndex(const juce::String &id) const {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (int index = 0; index < static_cast<int>(groupBuses_.size()); ++index)
    if (groupBuses_[static_cast<std::size_t>(index)]->id == id)
      return index;
  return -1;
}

bool MixerModel::renameGroupBus(const juce::String &id,
                                const juce::String &name) {
  const auto cleaned = name.trim();
  if (cleaned.isEmpty())
    return false;
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (auto &bus : groupBuses_) {
    if (bus->id == id) {
      bus->name = cleaned;
      return true;
    }
  }
  return false;
}

bool MixerModel::moveGroupBus(const juce::String &id, int newIndex) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  const auto found = std::find_if(
      groupBuses_.begin(), groupBuses_.end(),
      [&id](const auto &bus) { return bus->id == id; });
  if (found == groupBuses_.end())
    return false;
  const int oldIndex = static_cast<int>(found - groupBuses_.begin());
  newIndex = juce::jlimit(0, static_cast<int>(groupBuses_.size()) - 1,
                         newIndex);
  if (oldIndex == newIndex)
    return true;
  auto bus = std::move(*found);
  groupBuses_.erase(found);
  groupBuses_.insert(groupBuses_.begin() + newIndex, std::move(bus));
  commitAudioGraph();
  return true;
}

bool MixerModel::setGroupBusGain(const juce::String &id, float gainDb) {
  if (auto *bus = getGroupBus(id)) {
    bus->setGainDb(gainDb);
    return true;
  }
  return false;
}

bool MixerModel::setGroupBusMute(const juce::String &id, bool muted) {
  if (auto *bus = getGroupBus(id)) {
    bus->setMuted(muted);
    return true;
  }
  return false;
}

bool MixerModel::setGroupBusSolo(const juce::String &id, bool soloed) {
  if (auto *bus = getGroupBus(id)) {
    bus->setSoloed(soloed);
    return true;
  }
  return false;
}

GroupBus *MixerModel::getGroupBus(const juce::String &id) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (auto &bus : groupBuses_)
    if (bus->id == id)
      return bus.get();
  return nullptr;
}

std::vector<GroupBus *> MixerModel::getAllGroupBuses() {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  std::vector<GroupBus *> result;
  result.reserve(groupBuses_.size());
  for (auto &bus : groupBuses_)
    result.push_back(bus.get());
  return result;
}

bool MixerModel::setStripDirectOutput(const juce::String &stripId,
                                      const juce::String &busId) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  auto strip = std::find_if(strips_.begin(), strips_.end(),
                            [&stripId](const auto &item) {
                              return item->id == stripId;
                            });
  if (strip == strips_.end())
    return false;
  if (busId.isNotEmpty()) {
    const auto bus = std::find_if(groupBuses_.begin(), groupBuses_.end(),
                                  [&busId](const auto &item) {
                                    return item->id == busId;
                                  });
    if (bus == groupBuses_.end())
      return false;
  }
  (*strip)->directOutputBusId = busId;
  commitAudioGraph();
  return true;
}

void MixerModel::refreshAudioRouting() {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  commitAudioGraph();
}

juce::String MixerModel::groupBusesToJson() const {
  juce::Array<juce::var> values;
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (const auto &bus : groupBuses_)
    values.add(bus->toJson());
  return juce::JSON::toString(juce::var(values), true);
}

juce::AudioPluginFormatManager &MixerModel::getFormatManager() {
  return formatManager_;
}

int MixerModel::size() const {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  return static_cast<int>(strips_.size());
}

juce::String MixerModel::toJson() const {
  juce::Array<juce::var> arr;
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (const auto &s : strips_)
    arr.add(s->toJson());
  return juce::JSON::toString(juce::var(arr), true);
}

juce::var MixerModel::meterLevels() const {
  auto *result = new juce::DynamicObject();
  auto *strips = new juce::DynamicObject();
  auto *buses = new juce::DynamicObject();
  result->setProperty("strips", juce::var(strips));
  result->setProperty("buses", juce::var(buses));
  // This is the message-thread collection lock, never acquired by rendering.
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (const auto &strip : strips_) {
    juce::Array<juce::var> levels;
    levels.add(static_cast<double>(strip->peakDb()));
    levels.add(static_cast<double>(strip->peakHoldDb()));
    strips->setProperty(strip->id, juce::var(levels));
  }
  for (const auto &bus : groupBuses_) {
    juce::Array<juce::var> levels;
    levels.add(static_cast<double>(bus->peakDb()));
    levels.add(static_cast<double>(bus->peakHoldDb()));
    buses->setProperty(bus->id, juce::var(levels));
  }
  result->setProperty("masterPeakDb", static_cast<double>(masterAudio_.peakDb()));
  return juce::var(result);
}

juce::var MixerModel::pluginTimings(double now) {
  juce::Array<juce::var> rows;
  // Collection ownership is message-thread only; audio never takes this lock.
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (const auto &strip : strips_) {
    const auto owner = strip->layerName + " / " + strip->library +
                       " [" + strip->id + "]";
    strip->appendPluginTimings(rows, owner, now);
  }
  for (const auto &bus : groupBuses_)
    bus->audioEngine().appendPluginTimings(rows, "Bus: " + bus->name, now);
  masterAudio_.appendPluginTimings(rows, now);
  return juce::var(rows);
}

double MixerModel::maximumPathLatencyMs() const {
  auto graph = audioGraph_.read();
  double longest = 0;
  if (graph.get()) for (const auto &route : graph.get()->stripRoutes) {
    const auto path = route.strip->audioEngine().latencyMs() +
        (route.destination ? route.destination->audioEngine().latencyMs() : 0.0);
    longest = std::max(longest, path);
  }
  return longest + masterAudio_.latencyMs();
}

void MixerModel::processBlock(juce::AudioBuffer<float> &audioBuffer,
                              double currentTime) {
  AudioProcessingGate::Render render;
  if (!render) { audioBuffer.clear(); return; }
  if (audioBuffer.getNumSamples() <= 0 || audioBuffer.getNumSamples() > currentBlockSize_) {
    audioBuffer.clear();
    return;
  }
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  // Pre-compute whether any strip is soloed
  bool anySoloed = false;
  for (const auto &route : graph->stripRoutes) {
    if (route.strip->isSoloed()) {
      anySoloed = true;
      break;
    }
  }

  bool anyBusSoloed = false;
  for (auto *bus : graph->groupBuses) {
    bus->beginBlock(audioBuffer.getNumSamples());
    anyBusSoloed |= bus->isSoloed();
  }

  for (const auto &route : graph->stripRoutes) {
    const bool directPathAudible = !anyBusSoloed || route.destination != nullptr;
    auto &destination = route.destination ? route.destination->inputBuffer()
                                          : audioBuffer;
    route.strip->processBlock(destination, currentTime, anySoloed,
                              directPathAudible);
  }

  for (auto *bus : graph->groupBuses)
    bus->processTo(audioBuffer, !anyBusSoloed || bus->isSoloed());

  masterAudio_.processBlock(audioBuffer);
}

void MixerModel::prepareToPlay(double sampleRate, int blockSize) {
  AudioProcessingGate::Control control;
  std::lock_guard<std::mutex> lock(stripsMutex_);
  currentSampleRate_ = sampleRate;
  currentBlockSize_ = blockSize;
  for (auto &strip : strips_) {
    strip->prepareToPlay(sampleRate, blockSize);
  }
  for (auto &bus : groupBuses_)
    bus->prepareToPlay(sampleRate, blockSize);
  masterAudio_.prepareToPlay(sampleRate, blockSize);
}

void MixerModel::routeNoteEvent(int port, int channel,
                                const juce::MidiMessage &msg,
                                double triggerTime) {
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  for (const auto &route : graph->stripRoutes) {
    auto *strip = route.strip;
    if (strip->matchesInput(port, channel)) {
      strip->addDelayedMessage(triggerTime, msg);
    }
  }
}

void MixerModel::routeCCEvent(int port, int channel,
                              const juce::MidiMessage &msg) {
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  double now = juce::Time::getMillisecondCounterHiRes();
  for (const auto &route : graph->stripRoutes) {
    auto *strip = route.strip;
    if (strip->matchesInput(port, channel)) {
      strip->addDelayedMessage(now, msg);
    }
  }
}

juce::MidiMessage
MixerModel::instructionToMidi(const fiddle::MidiInstruction &instr,
                              int noteChannel) {
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
    return juce::MidiMessage::pitchWheel(ch, (instr.param1() << 7) |
                                                 instr.param2());
  case fiddle::MidiInstruction::CHANNEL_PRESSURE:
    return juce::MidiMessage::channelPressureChange(ch, instr.param1());
  default:
    return juce::MidiMessage(); // empty
  }
}

void MixerModel::captureInstruction(MidiCaptureLog &log, double timeMs,
                                    const fiddle::MidiInstruction &instr,
                                    int noteChannel,
                                    const juce::String &label) {
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
    log.record(timeMs, ch, CapturedMidiEvent::ProgramChange, instr.param1(), 0,
               label);
    break;
  default:
    break;
  }
}

void MixerModel::routeAnnotatedNoteOn(int port, int channel, fiddle::Note &inputNote,
                                      double triggerTimeMs) {
  // ── Read tonal context from the harmonic analysis service ─────────────
  // The service is fed by MainComponent (onNoteEvent), so we just read.
  TonalContext tonalCtx;
  if (harmonicService_) {
    TonalContext keyCtx = harmonicService_->getContext();
    tonalCtx = HarmonicAnalysisService::annotateNote(
        keyCtx, static_cast<int>(inputNote.note_number()));
  }

  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  // Preserve Dorico's structured input before any strip-specific Lua or
  // expression-map annotator mutates the shared Note.
  const auto incomingRecord =
      makeIncomingNoteRecord(inputNote, currentSampleRate_, false);

  for (auto *strip : graph->strips) {
    if (!strip->matchesInput(port, channel))
      continue;

    auto midiLock = strip->lockMidiState();
    // Each layer gets the same original input, not a previous layer's mutation.
    auto note = inputNote;
    // Run the annotator — it populates note.pre_note() and during_note()
    AnnotatorContext ctx;
    ctx.stripChannel = channel + 1; // convert 0-based to 1-based
    ctx.currentTimeMs = triggerTimeMs;
    ctx.sampleRate = currentSampleRate_;
    ctx.tonalContext = tonalCtx; // inject tonal context
    strip->annotator->onNoteStart(note, ctx);

    // The inspector always receives the input note. If this strip has an
    // expression map, enrich it with the resolver's decision and MIDI output.
    auto stripInput = incomingRecord;
    stripInput.expressionMapAssigned = strip->expressionMap != nullptr;
    strip->pushAnnotation(mergeNoteInspection(
        stripInput, strip->annotator->lastAnnotationRecord()));

    // ── Latching keyswitches: release old, latch new ──────────────
    // Collect the new keyswitch notes from pre_note instructions
    std::set<MixerStrip::HeldKS> newKS;
    for (const auto &instr : note.pre_note()) {
      if (instr.type() == fiddle::MidiInstruction::KEY_SWITCH) {
        int ch = instr.channel() > 0 ? instr.channel() : (int)note.channel();
        newKS.insert({ch, instr.param1()});
      }
    }

    // Schedule pre_note instructions first (new keyswitches before releasing
    // old)
    for (const auto &instr : note.pre_note()) {
      double t = triggerTimeMs + instr.delay_ms();
      juce::MidiMessage msg = instructionToMidi(instr, (int)note.channel());
      strip->addDelayedMessage(t, msg);
      captureInstruction(strip->emittedCapture, t, instr, (int)note.channel());
    }

    // Release keyswitches that are no longer needed (after new ones are sent)
    if (!newKS.empty()) {
      for (const auto &old : strip->heldKeyswitchNotes) {
        if (newKS.find(old) == newKS.end()) {
          double releaseTime = triggerTimeMs - 1.0;
          strip->addDelayedMessage(
              releaseTime, juce::MidiMessage::noteOff(
                               old.channel, old.noteNumber, (juce::uint8)64));
          strip->emittedCapture.record(releaseTime, old.channel,
                                       CapturedMidiEvent::NoteOff,
                                       old.noteNumber, 64);
        }
      }
      strip->heldKeyswitchNotes = newKS;
    }

    // Schedule during_note instructions before noteOn (matches Dorico ordering)
    for (const auto &instr : note.during_note()) {
      double t = triggerTimeMs + instr.delay_ms();
      juce::MidiMessage msg = instructionToMidi(instr, (int)note.channel());
      strip->addDelayedMessage(t, msg);
      captureInstruction(strip->emittedCapture, t, instr, (int)note.channel());
      // Track any keyswitches in during_note too
      if (instr.type() == fiddle::MidiInstruction::KEY_SWITCH) {
        int ch = instr.channel() > 0 ? instr.channel() : (int)note.channel();
        strip->heldKeyswitchNotes.insert({ch, instr.param1()});
      }
    }

    // Schedule the noteOn itself
    juce::MidiMessage noteOnMsg =
        juce::MidiMessage::noteOn((int)note.channel(), (int)note.note_number(),
                                  (juce::uint8)note.start_velocity());
    strip->addDelayedMessage(triggerTimeMs, noteOnMsg);
    strip->emittedCapture.record(
        triggerTimeMs, (int)note.channel(), CapturedMidiEvent::NoteOn,
        (int)note.note_number(), (int)note.start_velocity());

    // If an annotator (e.g. Lua plugin) scheduled an early note-off,
    // schedule it now at note-on time + scheduled ms.
    double noteOffMs = strip->annotator->scheduledNoteOffMs();
    if (noteOffMs > 0) {
      double noteOffTime = triggerTimeMs + noteOffMs;
      juce::MidiMessage earlyNoteOff = juce::MidiMessage::noteOff(
          (int)note.channel(), (int)note.note_number(), (juce::uint8)64);
      strip->addDelayedMessage(noteOffTime, earlyNoteOff);
      strip->emittedCapture.record(noteOffTime, (int)note.channel(),
                                   CapturedMidiEvent::NoteOff,
                                   (int)note.note_number(), 64);
    }
  }
}

void MixerModel::routeAnnotatedNoteOff(int port, int channel,
                                       fiddle::Note &inputNote,
                                       double triggerTimeMs) {
  // Note removal from the HMM is handled internally by the
  // HarmonicAnalysisService (via onNoteEvent called from MainComponent).
  // No action needed here.

  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  for (auto *strip : graph->strips) {
    if (!strip->matchesInput(port, channel))
      continue;

    auto midiLock = strip->lockMidiState();
    auto note = inputNote;
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
    double adjustedTime = triggerTimeMs + strip->annotator->durationAdjustMs();

    // Determine the switch label from the most recent annotation
    juce::String switchLabel;
    if (!strip->recentAnnotations_.empty()) {
      const auto &baseName = strip->recentAnnotations_.back().baseSwitchName;
      if (!baseName.empty())
        switchLabel = juce::String(baseName);
    }

    // Schedule the noteOff at adjusted time (with switch label, matching
    // Dorico)
    juce::MidiMessage noteOffMsg = juce::MidiMessage::noteOff(
        (int)note.channel(), (int)note.note_number(), (juce::uint8)64);
    strip->addDelayedMessage(adjustedTime, noteOffMsg);
    strip->emittedCapture.record(adjustedTime, (int)note.channel(),
                                 CapturedMidiEvent::NoteOff,
                                 (int)note.note_number(), 64, switchLabel);

    // Schedule post_note instructions (relative to adjusted noteOff time)
    for (const auto &instr : note.post_note()) {
      double t = adjustedTime + instr.delay_ms();
      juce::MidiMessage msg = instructionToMidi(instr, (int)note.channel());
      strip->addDelayedMessage(t, msg);
      captureInstruction(strip->emittedCapture, t, instr, (int)note.channel());
      // Track any keyswitches in post_note as latched
      if (instr.type() == fiddle::MidiInstruction::KEY_SWITCH) {
        int ch = instr.channel() > 0 ? instr.channel() : (int)note.channel();
        strip->heldKeyswitchNotes.insert({ch, instr.param1()});
      }
    }
  }
}

void MixerModel::releaseAllKeyswitches(double triggerTimeMs) {
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  for (auto *strip : graph->strips) {
    auto midiLock = strip->lockMidiState();
    for (const auto &ks : strip->heldKeyswitchNotes) {
      juce::MidiMessage noteOffMsg = juce::MidiMessage::noteOff(
          ks.channel, ks.noteNumber, (juce::uint8)64);
      strip->addDelayedMessage(triggerTimeMs, noteOffMsg);
      strip->emittedCapture.record(triggerTimeMs, ks.channel,
                                   CapturedMidiEvent::NoteOff, ks.noteNumber,
                                   64);
    }
    strip->heldKeyswitchNotes.clear();
  }
}

void MixerModel::gracefulStop(const std::vector<fiddle::Note> &activeNotes,
                              double releaseMs,
                              const std::map<int, uint8_t> &channelCC1) {
  auto graphRead = audioGraph_.read();
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
    auto midiLock = strip->lockMidiState();
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
    if (startVal <= 0)
      continue;

    for (int i = 1; i <= kRampSteps; ++i) {
      const int val = startVal - (startVal * i / kRampSteps);
      const double t = now + (stepMs * i);
      for (auto *strip : graph->strips) {
        if (strip->matchesInput(port, ch0)) {
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
      if (strip->matchesInput(port, channel)) {
        strip->addDelayedMessage(
            noteOffTime, juce::MidiMessage::noteOff((int)note.channel(),
                                                    (int)note.note_number(),
                                                    (juce::uint8)64));
      }
    }
  }

  // 5. Schedule All Sound Off + Reset Controllers shortly after note-offs
  const double killTime = noteOffTime + 50.0;
  for (auto *strip : graph->strips) {
    for (int ch = 1; ch <= 16; ++ch) {
      strip->addDelayedMessage(killTime, juce::MidiMessage::allSoundOff(ch));
      strip->addDelayedMessage(killTime,
                               juce::MidiMessage::allControllersOff(ch));
    }
  }
}

void MixerModel::routeAnnotatedCC(int port, int channel,
                                  const fiddle::MidiEvent &event,
                                  const juce::MidiMessage &msg,
                                  double triggerTimeMs) {
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  for (auto *strip : graph->strips) {
    if (!strip->matchesInput(port, channel))
      continue;

    AnnotatorContext ctx;
    ctx.stripChannel = channel + 1;
    ctx.currentTimeMs = triggerTimeMs;

    auto midiLock = strip->lockMidiState();
    if (strip->annotator->onCC(event, ctx)) {
      strip->addDelayedMessage(triggerTimeMs, msg);
      if (event.has_cc()) {
        strip->emittedCapture.record(triggerTimeMs, channel + 1,
                                     CapturedMidiEvent::CC,
                                     (int)event.cc().controller_number(),
                                     (int)event.cc().controller_value());
      }
    }
  }
}

void MixerModel::recordIncomingMidi(int port, int channel,
                                    CapturedMidiEvent::Type type, int p1,
                                    int p2, double timeMs) {
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  for (auto *strip : graph->strips) {
    if (!strip->matchesInput(port, channel))
      continue;

    auto midiLock = strip->lockMidiState();
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

void MixerModel::allNotesOff() {
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  for (auto *strip : graph->strips) {
    strip->allNotesOff();
  }
}

void MixerModel::panicStrips(int port, int channel) {
  auto graphRead = audioGraph_.read();
  auto *graph = graphRead.get();
  if (!graph)
    return;

  for (auto *strip : graph->strips) {
    if (strip->matchesInput(port, channel)) {
      strip->allNotesOff();
    }
  }
}

bool MixerModel::setStripMute(const juce::String &id, bool mute) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (auto &s : strips_) {
    if (s->id == id) {
      s->setMuted(mute);
      return true;
    }
  }
  return false;
}

bool MixerModel::setStripSolo(const juce::String &id, bool solo) {
  std::lock_guard<std::mutex> lock(stripsMutex_);
  for (auto &s : strips_) {
    if (s->id == id) {
      s->setSoloed(solo);
      return true;
    }
  }
  return false;
}

void MixerModel::syncStripsToInstruments(
    const MasterInstrumentList &masterList) {
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
        if (port == 0 && ch == 0)
          continue;

        juce::String label = obj->getProperty("label").toString();
        juce::String family = obj->getProperty("family").toString();
        bool solo = (bool)obj->getProperty("isSolo");
        expected.push_back({port, ch, label, family, solo});
        expectedSet.insert({port, ch});
      }
    }
  }

  std::unique_lock<std::mutex> lock(stripsMutex_);
  std::vector<std::shared_ptr<MixerStrip>> removedStrips;

  // Remove strips whose port/channel is no longer in the expected set
  for (auto it = strips_.begin(); it != strips_.end();) {
    const auto state = (*it)->realtimeState();
    auto key = std::make_pair(state.inputPort, state.inputChannel);
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
    const auto state = s->realtimeState();
    auto key = std::make_pair(state.inputPort, state.inputChannel);
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
      strip->updatePluginSlotId();
      strip->library = "";
      strip->family = entry.family;
      strip->isSolo = entry.isSolo;
      strip->setInputAssignment(entry.port, entry.channel);
      strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
      strips_.push_back(std::move(strip));
    }
  }
  commitAudioGraph();
  lock.unlock();

  if (!removedStrips.empty()) {
    for (auto &strip : removedStrips)
      strip->unloadPlugin();
    std::lock_guard<std::mutex> trashLock(retiredStripMutex_);
    for (auto &strip : removedStrips)
      trashStrips_.push_back(std::move(strip));
  }
}

void MixerModel::setHarmonicService(HarmonicAnalysisService *service) {
  harmonicService_ = service;
}

void MixerModel::commitAudioGraph() {
  auto current = std::make_unique<ActiveAudioGraph>();
  current->stripOwners = strips_;
  current->busOwners = groupBuses_;
  std::map<juce::String, GroupBus *> busById;
  for (auto &bus : groupBuses_) {
    current->groupBuses.push_back(bus.get());
    busById.emplace(bus->id, bus.get());
  }
  for (auto &s : strips_) {
    current->strips.push_back(s.get());
    GroupBus *destination = nullptr;
    if (s->directOutputBusId.isNotEmpty()) {
      const auto found = busById.find(s->directOutputBusId);
      if (found != busById.end())
        destination = found->second;
      else
        s->directOutputBusId.clear();
    }
    s->setDownstreamLatencySamples(
        destination ? destination->audioEngine().latencySamples() : 0);
    current->stripRoutes.push_back({s.get(), destination});
  }
  audioGraph_.publish(std::move(current));
}

void MixerModel::timerCallback() {
  std::vector<std::shared_ptr<MixerStrip>> stripsToDelete;
  std::vector<std::shared_ptr<GroupBus>> busesToDelete;

  audioGraph_.reclaimRetiredWith(
      [this, &stripsToDelete, &busesToDelete] {
        std::lock_guard<std::mutex> lock(retiredStripMutex_);
        stripsToDelete.swap(trashStrips_);
        busesToDelete.swap(trashGroupBuses_);
      },
      [](ActiveAudioGraph &) {});

  // Reclamation always happens on the message thread, never in processBlock.
  {
    std::lock_guard<std::mutex> lock(stripsMutex_);
    for (auto &strip : strips_)
      strip->reclaimRetiredPluginRuntimes();
  }
  stripsToDelete.clear();
  busesToDelete.clear();
}

int MixerModel::getPlaybackDelayMs() const { return playbackDelayMs_; }

void MixerModel::setPlaybackDelayMs(int ms) { playbackDelayMs_ = std::clamp(ms, 0, 5000); }

} // namespace fiddle
