#include "MixerStrip.h"

#include "ExpressionMapAnnotator.h"
#include "LuaAnnotator.h"
#include "LuaPlugin.h"
#include "PluginEditorWindow.h"
#include <filesystem>

namespace fiddle {

MixerStrip::MixerStrip() { rebuildAnnotatorChain(); }

MixerStrip::~MixerStrip() {
  if (lifetimeToken_)
    *lifetimeToken_ = false;

  editorWindow_.reset();
  publishPluginRuntime(nullptr);
  jassert(pluginRuntime_.readerCount() == 0);
  reclaimRetiredPluginRuntimes();
}

void MixerStrip::PluginChangeListener::audioProcessorParameterChanged(
    juce::AudioProcessor *, int, float) {
  if (listenerCapableUids && pluginUid != 0)
    listenerCapableUids->insert(pluginUid);
  if (onDirty)
    onDirty();
}

void MixerStrip::PluginChangeListener::audioProcessorChanged(
    juce::AudioProcessor *,
    const juce::AudioProcessorListener::ChangeDetails &d) {
  if (d.nonParameterStateChanged) {
    if (listenerCapableUids && pluginUid != 0)
      listenerCapableUids->insert(pluginUid);
    if (onDirty)
      onDirty();
  }
}

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
  return cachedPluginState_;
}

bool MixerStrip::hasPlugin() const noexcept {
  return pluginInstance_ != nullptr;
}

bool MixerStrip::capturePluginState(juce::MemoryBlock &destination) const {
  destination.reset();
  if (!pluginInstance_)
    return false;
  pluginInstance_->getStateInformation(destination);
  return true;
}

bool MixerStrip::applyPluginState(const void *data, int sizeInBytes) {
  if (!pluginInstance_ || data == nullptr || sizeInBytes <= 0)
    return false;
  pluginInstance_->setStateInformation(data, sizeInBytes);
  refreshPluginStateCache();
  return true;
}

bool MixerStrip::setPluginProgram(int programIndex) {
  if (!pluginInstance_)
    return false;
  pluginInstance_->setCurrentProgram(programIndex);
  refreshPluginStateCache();
  return true;
}

void MixerStrip::refreshPluginStateCache() {
  if (pluginInstance_) {
    cachedPluginState_.reset();
    pluginInstance_->getStateInformation(cachedPluginState_);
  } else {
    cachedPluginState_.reset();
  }
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
  if (auto *runtime = pluginRuntime_.activeForWriter()) {
    int maxChannels =
        juce::jmax(runtime->instance->getTotalNumInputChannels(),
                   runtime->instance->getTotalNumOutputChannels(), 2);
    runtime->tempBuffer.setSize(maxChannels, blockSize);
    runtime->instance->prepareToPlay(sampleRate, blockSize);
  }
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

  auto runtimeRead = pluginRuntime_.read();
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
        float prev = peakDb_.load(std::memory_order_relaxed);
        peakDb_.store(juce::jmax(-120.0f, prev - decay),
                      std::memory_order_relaxed);
        float prevHold = peakHoldDb_.load(std::memory_order_relaxed);
        peakHoldDb_.store(juce::jmax(-120.0f, prevHold - holdDecay),
                          std::memory_order_relaxed);
      } else {
        // Apply fader gain and mix down to the main host buffer
        float db = gainDb_.load(std::memory_order_relaxed);
        if (db <= -120.0f) {
          // Fader at silence — decay peak
          float decay = 20.0f * numSamples / (float)sampleRate;
          float holdDecay = 3.0f * numSamples / (float)sampleRate;
          float prev = peakDb_.load(std::memory_order_relaxed);
          peakDb_.store(juce::jmax(-120.0f, prev - decay),
                        std::memory_order_relaxed);
          float prevHold = peakHoldDb_.load(std::memory_order_relaxed);
          peakHoldDb_.store(juce::jmax(-120.0f, prevHold - holdDecay),
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
          float prev = peakDb_.load(std::memory_order_relaxed);
          peakDb_.store(juce::jmax(blockDb, prev - decay),
                        std::memory_order_relaxed);
          // Hold decay: ~5 dB/sec (slow)
          float holdDecay = 3.0f * numSamples / (float)sampleRate;
          float prevHold = peakHoldDb_.load(std::memory_order_relaxed);
          peakHoldDb_.store(juce::jmax(blockDb, prevHold - holdDecay),
                            std::memory_order_relaxed);
        }
      }
    }
  }
}

void MixerStrip::loadPlugin(const juce::PluginDescription &desc,
                            juce::AudioPluginFormatManager &formatManager,
                            std::function<void(bool)> onComplete) {

  std::weak_ptr<bool> weakToken = lifetimeToken_;
  formatManager.createPluginInstanceAsync(
      desc, currentSampleRate_.load(std::memory_order_relaxed),
      currentBlockSize_.load(std::memory_order_relaxed),
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
        editorWindow_.reset();

        int maxChannels = juce::jmax(instance->getTotalNumInputChannels(),
                                     instance->getTotalNumOutputChannels(), 2);

        const auto sampleRate =
            currentSampleRate_.load(std::memory_order_relaxed);
        const auto blockSize =
            currentBlockSize_.load(std::memory_order_relaxed);
        instance->prepareToPlay(sampleRate, blockSize);

        auto runtime = std::make_unique<PluginRuntime>();
        runtime->tempBuffer.setSize(maxChannels, blockSize);
        runtime->instance = std::move(instance);

        // Register the listener before publication so every visible plugin
        // has a fully initialized runtime.
        pluginChangeListener_.stripId = id;
        pluginChangeListener_.pluginUid = desc.uniqueId;
        runtime->instance->addListener(&pluginChangeListener_);

        pluginInstance_ = runtime->instance.get();
        pluginUid = desc.uniqueId;
        publishPluginRuntime(std::move(runtime));

        // Populate initial cache (may be overwritten by setStateInformation
        // in the caller's onComplete callback)
        refreshPluginStateCache();

        // std::cerr << "[MixerStrip " << id << "] Loaded (Async): " <<
        // desc.name
        //           << std::endl;
        if (onComplete)
          onComplete(true);
      });
}

void MixerStrip::unloadPlugin() {
  editorWindow_.reset();
  pluginInstance_ = nullptr;
  publishPluginRuntime(nullptr);
  pluginUid = 0;
  cachedPluginState_.reset();
}

void MixerStrip::reclaimRetiredPluginRuntimes() {
  pluginRuntime_.reclaimRetired([this](PluginRuntime &runtime) {
    runtime.instance->removeListener(&pluginChangeListener_);
    runtime.instance->releaseResources();
  });
}

uint64_t MixerStrip::droppedMidiMessageCount() const noexcept {
  return midiScheduler_.droppedMessageCount();
}

void MixerStrip::showEditor() {
  if (!pluginInstance_)
    return;
  if (editorWindow_) {
    editorWindow_->setVisible(true);
    editorWindow_->toFront(true);
  } else if (auto *editor = pluginInstance_->createEditorAndMakeActive()) {
    editorWindow_ = std::make_unique<PluginEditorWindow>(library, editor);
  }
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
  obj->setProperty("gainDb", (double)state.gainDb);
  obj->setProperty("peakDb", (double)state.peakDb);
  obj->setProperty("peakHoldDb", (double)state.peakHoldDb);
  obj->setProperty("expressionMapName",
                   expressionMap ? juce::String(expressionMap->name) : "");
  obj->setProperty("expressionMapEntityID",
                   expressionMap ? juce::String(expressionMap->entityID) : "");

  if (pluginInstance_) {
    int prog = pluginInstance_->getCurrentProgram();
    int numProgs = pluginInstance_->getNumPrograms();
    juce::String progName = pluginInstance_->getProgramName(prog);
    obj->setProperty("programIndex", prog);
    obj->setProperty("programName", progName);
    obj->setProperty("numPrograms", numProgs);

    // Emit all program names so the UI can show a dropdown.
    juce::Array<juce::var> names;
    for (int i = 0; i < numProgs; ++i)
      names.add(pluginInstance_->getProgramName(i));
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

void MixerStrip::publishPluginRuntime(std::unique_ptr<PluginRuntime> runtime) {
  pluginRuntime_.publish(std::move(runtime));
}

} // namespace fiddle
