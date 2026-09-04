#include "MixerStrip.h"

#include "ExpressionMapAnnotator.h"
#include "LuaAnnotator.h"
#include "LuaPlugin.h"
#include <filesystem>

namespace fiddle {

MixerStrip::MixerStrip() { rebuildAnnotatorChain(); }

MixerStrip::~MixerStrip() = default;

MixerStrip::RealtimeState MixerStrip::realtimeState() const noexcept {
  const auto assignment = inputAssignment_.load(std::memory_order_relaxed);
  return {
      active_.load(std::memory_order_relaxed),
      muted_.load(std::memory_order_relaxed),
      soloed_.load(std::memory_order_relaxed),
      static_cast<int32_t>(assignment >> 32),
      static_cast<int32_t>(assignment),
      gainDb_.load(std::memory_order_relaxed),
      peakDb_.load(std::memory_order_relaxed),
      peakHoldDb_.load(std::memory_order_relaxed),
  };
}

bool MixerStrip::isActive() const noexcept {
  return active_.load(std::memory_order_relaxed);
}

void MixerStrip::setActive(bool shouldBeActive) noexcept {
  active_.store(shouldBeActive, std::memory_order_relaxed);
}

bool MixerStrip::isMuted() const noexcept {
  return muted_.load(std::memory_order_relaxed);
}

void MixerStrip::setMuted(bool shouldBeMuted) noexcept {
  muted_.store(shouldBeMuted, std::memory_order_relaxed);
}

bool MixerStrip::isSoloed() const noexcept {
  return soloed_.load(std::memory_order_relaxed);
}

void MixerStrip::setSoloed(bool shouldBeSoloed) noexcept {
  soloed_.store(shouldBeSoloed, std::memory_order_relaxed);
}

int MixerStrip::inputPort() const noexcept {
  return static_cast<int32_t>(
      inputAssignment_.load(std::memory_order_relaxed) >> 32);
}

int MixerStrip::inputChannel() const noexcept {
  return static_cast<int32_t>(inputAssignment_.load(std::memory_order_relaxed));
}

bool MixerStrip::matchesInput(int port, int channel) const noexcept {
  return inputAssignment_.load(std::memory_order_relaxed) ==
         packInputAssignment(port, channel);
}

void MixerStrip::setInputAssignment(int port, int channel) noexcept {
  inputAssignment_.store(packInputAssignment(port, channel),
                         std::memory_order_relaxed);
}

float MixerStrip::gainDb() const noexcept {
  return gainDb_.load(std::memory_order_relaxed);
}

void MixerStrip::setGainDb(float gain) noexcept {
  gainDb_.store(gain, std::memory_order_relaxed);
}

float MixerStrip::peakDb() const noexcept {
  return peakDb_.load(std::memory_order_relaxed);
}

float MixerStrip::peakHoldDb() const noexcept {
  return peakHoldDb_.load(std::memory_order_relaxed);
}

juce::MemoryBlock MixerStrip::cachedPluginState() const {
  return instrumentSlot_.cachedState();
}

bool MixerStrip::hasPlugin() const noexcept {
  return instrumentSlot_.hasProcessor();
}

bool MixerStrip::capturePluginState(juce::MemoryBlock &destination) const {
  return instrumentSlot_.captureState(destination);
}

bool MixerStrip::applyPluginState(const void *data, int sizeInBytes) {
  return instrumentSlot_.applyState(data, sizeInBytes);
}

bool MixerStrip::setPluginProgram(int programIndex) {
  return instrumentSlot_.setProgram(programIndex);
}

void MixerStrip::setPluginBypassed(bool bypassed) noexcept {
  instrumentSlot_.setBypassed(bypassed);
}

bool MixerStrip::isPluginBypassed() const noexcept {
  return instrumentSlot_.isBypassed();
}

HostedPluginStatus MixerStrip::pluginStatus() const noexcept {
  return instrumentSlot_.status();
}

const PluginCompatibility &MixerStrip::pluginCompatibility() const noexcept {
  return instrumentSlot_.compatibility();
}

void MixerStrip::updatePluginSlotId() {
  instrumentSlot_.setId(PluginSlotId::instrumentFor(id));
}

bool MixerStrip::consumePluginChangeNotification() noexcept {
  return instrumentSlot_.consumeChangeNotification();
}

bool MixerStrip::consumePluginExplicitEditNotification() noexcept {
  return instrumentSlot_.consumeExplicitEditNotification();
}

bool MixerStrip::consumePluginNonParameterStateChangeNotification() noexcept {
  return instrumentSlot_.consumeNonParameterStateChangeNotification();
}

uint64_t MixerStrip::pluginParameterFingerprint() const {
  return instrumentSlot_.parameterFingerprint();
}

void MixerStrip::refreshPluginStateCache() {
  instrumentSlot_.refreshStateCache();
}

void MixerStrip::setExpressionMap(std::shared_ptr<ExpressionMapData> em) {
  expressionMap = std::move(em);
  if (expressionMap) {
    incomingTracker = std::make_unique<IncomingSwitchTracker>(expressionMap);
  } else {
    incomingTracker.reset();
  }
  rebuildAnnotatorChain();
}

void MixerStrip::addLuaPlugin(std::shared_ptr<LuaPlugin> plugin) {
  if (plugin) {
    luaPlugins.push_back(std::move(plugin));
    rebuildAnnotatorChain();
  }
}

void MixerStrip::removeLuaPlugin(size_t index) {
  if (index < luaPlugins.size()) {
    luaPlugins.erase(luaPlugins.begin() + (ptrdiff_t)index);
    rebuildAnnotatorChain();
  }
}

void MixerStrip::insertLuaPlugin(size_t index,
                                 std::shared_ptr<LuaPlugin> plugin) {
  if (plugin) {
    if (index >= luaPlugins.size())
      luaPlugins.push_back(std::move(plugin));
    else
      luaPlugins.insert(luaPlugins.begin() + (ptrdiff_t)index,
                        std::move(plugin));
    rebuildAnnotatorChain();
  }
}

std::vector<std::string> MixerStrip::getLuaPluginFileNames() const {
  std::vector<std::string> names;
  for (const auto &p : luaPlugins) {
    std::filesystem::path fp(p->filePath());
    names.push_back(fp.filename().string());
  }
  return names;
}

void MixerStrip::rebuildAnnotatorChain() {
  auto chain = std::make_unique<AnnotatorChain>();
  for (auto &plugin : luaPlugins)
    chain->add(std::make_unique<LuaAnnotator>(plugin));
  if (expressionMap)
    chain->add(std::make_unique<ExpressionMapAnnotator>(expressionMap));
  else
    chain->add(std::make_unique<PassthroughAnnotator>());
  annotator = std::move(chain);
}

void MixerStrip::pushAnnotation(const AnnotationRecord &rec) {
  recentAnnotations_.push_back(rec);
  while (recentAnnotations_.size() > kMaxAnnotations)
    recentAnnotations_.pop_front();
}

void MixerStrip::clearAnnotations() {
  recentAnnotations_.clear();
  if (annotator)
    annotator->resetState();
}

juce::var MixerStrip::annotationRecordToVar(const AnnotationRecord &r) {
  auto *obj = new juce::DynamicObject();

  obj->setProperty("noteNumber", r.noteNumber);
  obj->setProperty("inputChannel", r.inputChannel);
  obj->setProperty("expressionMapAssigned", r.expressionMapAssigned);

  // Input techniques
  {
    juce::Array<juce::var> arr;
    for (const auto &t : r.inputTechniques)
      arr.add(juce::String(t));
    obj->setProperty("inputTechniques", juce::var(arr));
  }
  {
    auto *techniques = new juce::DynamicObject();
    for (const auto &[dimension, display] : r.receivedTechniques)
      techniques->setProperty(juce::String(dimension), juce::String(display));
    obj->setProperty("receivedTechniques", juce::var(techniques));
  }
  {
    auto *dimensions = new juce::DynamicObject();
    for (const auto &[dimension, value] : r.receivedDimensions)
      dimensions->setProperty(juce::String(dimension), value);
    obj->setProperty("receivedDimensions", juce::var(dimensions));
  }
  {
    juce::Array<juce::var> arr;
    for (const auto &dimension : r.defaultTechniqueDimensions)
      arr.add(juce::String(dimension));
    obj->setProperty("defaultTechniqueDimensions", juce::var(arr));
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

juce::String MixerStrip::getAnnotationRecordsAsJson() const {
  juce::Array<juce::var> arr;
  for (const auto &rec : recentAnnotations_)
    arr.add(annotationRecordToVar(rec));
  return juce::JSON::toString(juce::var(arr), true);
}

void MixerStrip::prepareToPlay(double sampleRate, int blockSize) {
  currentSampleRate_.store(sampleRate, std::memory_order_relaxed);
  currentBlockSize_.store(blockSize, std::memory_order_relaxed);
  processMidiBuffer_.ensureSize(128 * 1024);
  instrumentSlot_.prepareToPlay(sampleRate, blockSize);
  audioEngine_.prepareToPlay(sampleRate, blockSize);
}

void MixerStrip::addDelayedMessage(double triggerTime,
                                   const juce::MidiMessage &msg) {
  midiScheduler_.schedule(triggerTime, msg);
}

void MixerStrip::clearDelayedMessages() { midiScheduler_.requestClear(); }

void MixerStrip::allNotesOff() {
  heldKeyswitchNotes.clear();
  midiScheduler_.requestPanic();
}

void MixerStrip::processBlock(juce::AudioBuffer<float> &audioBuffer,
                              double currentTime, bool anySoloed) {
  // Effective audibility: active AND not muted AND (no solos active OR
  // this strip soloed)
  const bool audible = active_.load(std::memory_order_relaxed) &&
                       !muted_.load(std::memory_order_relaxed) &&
                       (!anySoloed || soloed_.load(std::memory_order_relaxed));
  processMidiBuffer_.clear();

  const int numSamples = audioBuffer.getNumSamples();
  const double sampleRate = currentSampleRate_.load(std::memory_order_relaxed);
  midiScheduler_.renderBlock(currentTime, sampleRate, numSamples,
                             processMidiBuffer_);

  auto runtimeRead = instrumentSlot_.readRuntime();
  auto *runtime = runtimeRead.get();
  if (runtime) {
    int numSamples = audioBuffer.getNumSamples();

    // Safety check just in case tempBuffer isn't sized
    if (runtime->scratchBuffer.getNumChannels() > 0 &&
        runtime->scratchBuffer.getNumSamples() >= numSamples) {
      runtime->scratchBuffer.clear();
      // Always process plugin (MIDI stays in sync even when muted)
      if (instrumentSlot_.isBypassed()) {
        // Bypassing an instrument means silence, while consuming scheduled
        // MIDI so stale events are not replayed when it is enabled again.
      } else {
        runtime->processor->processBlock(runtime->scratchBuffer,
                                         processMidiBuffer_);
      }

      const float db = gainDb_.load(std::memory_order_relaxed);
      const bool faderAudible = audible && db > -120.0f;
      const float effectiveGain =
          faderAudible ? juce::Decibels::decibelsToGain(db, -120.0f) : 0.0f;
      audioEngine_.processBlock(runtime->scratchBuffer, effectiveGain);

      if (!faderAudible) {
        // Strip is muted, solo-suppressed, or at -inf: decay meters and do
        // not sum it into the shared mix. The instrument and strip graph have
        // still processed the block, preserving MIDI and future effect tails.
        float decay = 20.0f * numSamples / (float)sampleRate;
        float holdDecay = 3.0f * numSamples / (float)sampleRate;
        float prev = peakDb_.load(std::memory_order_relaxed);
        peakDb_.store(juce::jmax(-120.0f, prev - decay),
                      std::memory_order_relaxed);
        float prevHold = peakHoldDb_.load(std::memory_order_relaxed);
        peakHoldDb_.store(juce::jmax(-120.0f, prevHold - holdDecay),
                          std::memory_order_relaxed);
      } else {
        // The per-strip graph has already applied the fader gain. Mix its
        // stereo output into the buffer that will feed the Master graph.
        const int channelsToSum =
            juce::jmin((int)audioBuffer.getNumChannels(),
                       runtime->scratchBuffer.getNumChannels());
        float blockPeak = 0.0f;
        for (int i = 0; i < channelsToSum; ++i) {
          audioBuffer.addFrom(i, 0, runtime->scratchBuffer, i, 0, numSamples);
          blockPeak = juce::jmax(
              blockPeak,
              runtime->scratchBuffer.getMagnitude(i, 0, numSamples));
        }
        const float blockDb =
            juce::Decibels::gainToDecibels(blockPeak, -120.0f);
        // Bar decay: ~20 dB/sec
        const float decay = 20.0f * numSamples / (float)sampleRate;
        const float prev = peakDb_.load(std::memory_order_relaxed);
        peakDb_.store(juce::jmax(blockDb, prev - decay),
                      std::memory_order_relaxed);
        // Hold decay: ~3 dB/sec (slow)
        const float holdDecay = 3.0f * numSamples / (float)sampleRate;
        const float prevHold = peakHoldDb_.load(std::memory_order_relaxed);
        peakHoldDb_.store(juce::jmax(blockDb, prevHold - holdDecay),
                          std::memory_order_relaxed);
      }
    }
  }
}

void MixerStrip::loadPlugin(const juce::PluginDescription &desc,
                            juce::AudioPluginFormatManager &formatManager,
                            std::function<void(bool)> onComplete) {
  instrumentSlot_.loadPlugin(
      desc, formatManager, currentSampleRate_.load(std::memory_order_relaxed),
      currentBlockSize_.load(std::memory_order_relaxed),
      [this, desc, onComplete = std::move(onComplete)](
          bool success, const juce::String &) {
        if (success)
          pluginUid = desc.uniqueId;
        else if (instrumentSlot_.status() == HostedPluginStatus::missing)
          pluginUid = instrumentSlot_.pluginUid();
        if (onComplete)
          onComplete(success);
      });
}

void MixerStrip::markPluginMissing(int uid, const juce::MemoryBlock &state,
                                   const juce::String &error) {
  juce::PluginDescription description;
  description.name = "Missing plug-in " + juce::String(uid);
  description.pluginFormatName = "VST3";
  description.uniqueId = uid;
  description.isInstrument = true;
  instrumentSlot_.markMissing(description, state, error);
  pluginUid = uid;
}

void MixerStrip::unloadPlugin() {
  instrumentSlot_.unload();
  pluginUid = 0;
}

void MixerStrip::reclaimRetiredPluginRuntimes() {
  instrumentSlot_.reclaimRetiredRuntimes();
}

uint64_t MixerStrip::droppedMidiMessageCount() const noexcept {
  return midiScheduler_.droppedMessageCount();
}

void MixerStrip::showEditor() {
  const auto title = library.isNotEmpty()
                         ? library
                         : instrumentSlot_.description().name;
  instrumentSlot_.showEditor(title);
}

juce::var MixerStrip::toJson() const {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("id", id);
  obj->setProperty("library", library);
  obj->setProperty("family", family);
  obj->setProperty("isSolo", isSolo);
  const auto state = realtimeState();
  obj->setProperty("active", state.active);
  obj->setProperty("muted", state.muted);
  obj->setProperty("soloed", state.soloed);
  obj->setProperty("inputPort", state.inputPort);
  obj->setProperty("inputChannel", state.inputChannel);
  obj->setProperty("pluginUid", pluginUid);
  obj->setProperty("hasPlugin", hasPlugin());
  obj->setProperty("pluginSlotId", instrumentSlot_.id().value);
  obj->setProperty("pluginStatus",
                   HostedPluginSlot::statusName(instrumentSlot_.status()));
  obj->setProperty("pluginBypassed", instrumentSlot_.isBypassed());
  obj->setProperty("pluginCompatibility",
                   instrumentSlot_.compatibility().toJson());
  obj->setProperty("gainDb", (double)state.gainDb);
  obj->setProperty("peakDb", (double)state.peakDb);
  obj->setProperty("peakHoldDb", (double)state.peakHoldDb);
  obj->setProperty("expressionMapName",
                   expressionMap ? juce::String(expressionMap->name) : "");
  obj->setProperty("expressionMapEntityID",
                   expressionMap ? juce::String(expressionMap->entityID) : "");

  if (auto *processor = instrumentSlot_.activeProcessor()) {
    int prog = processor->getCurrentProgram();
    int numProgs = processor->getNumPrograms();
    juce::String progName = processor->getProgramName(prog);
    obj->setProperty("programIndex", prog);
    obj->setProperty("programName", progName);
    obj->setProperty("numPrograms", numProgs);

    // Emit all program names so the UI can show a dropdown.
    juce::Array<juce::var> names;
    for (int i = 0; i < numProgs; ++i)
      names.add(processor->getProgramName(i));
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

} // namespace fiddle
