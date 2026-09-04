#include "MasterAudioEngine.h"

#include <algorithm>
#include <utility>

namespace fiddle {
namespace {

constexpr auto kNoUpdate = juce::AudioProcessorGraph::UpdateKind::none;

class MasterGainProcessor final : public juce::AudioProcessor {
public:
  explicit MasterGainProcessor(std::shared_ptr<std::atomic<float>> gain)
      : juce::AudioProcessor(
            BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        gain_(std::move(gain)) {}

  const juce::String getName() const override { return "Master Gain"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &) override {
    audio.applyGain(gain_->load(std::memory_order_relaxed));
  }
  void processBlock(juce::AudioBuffer<double> &audio,
                    juce::MidiBuffer &) override {
    audio.applyGain(
        static_cast<double>(gain_->load(std::memory_order_relaxed)));
  }
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

  bool isBusesLayoutSupported(const BusesLayout &layout) const override {
    return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
  }

private:
  std::shared_ptr<std::atomic<float>> gain_;
};

class HostedEffectGraphProcessor final : public juce::AudioProcessor {
public:
  explicit HostedEffectGraphProcessor(std::shared_ptr<HostedPluginSlot> slot)
      : juce::AudioProcessor(
            BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        slot_(std::move(slot)) {
    if (auto *processor = slot_->activeProcessor())
      setLatencySamples(processor->getLatencySamples());
  }

  const juce::String getName() const override { return "Hosted Master Effect"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &midi) override {
    slot_->processBlock(audio, midi);
  }
  void processBlock(juce::AudioBuffer<double> &audio,
                    juce::MidiBuffer &) override {
    audio.clear();
  }
  double getTailLengthSeconds() const override {
    if (auto *processor = slot_->activeProcessor())
      return processor->getTailLengthSeconds();
    return 0.0;
  }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

  bool isBusesLayoutSupported(const BusesLayout &layout) const override {
    return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
  }

private:
  std::shared_ptr<HostedPluginSlot> slot_;
};

bool connectStereo(juce::AudioProcessorGraph &graph,
                   const juce::AudioProcessorGraph::Node::Ptr &source,
                   const juce::AudioProcessorGraph::Node::Ptr &destination) {
  if (source == nullptr || destination == nullptr)
    return false;
  bool connected = true;
  for (int channel = 0; channel < 2; ++channel) {
    connected &= graph.addConnection(
        {{source->nodeID, channel}, {destination->nodeID, channel}}, kNoUpdate);
  }
  return connected;
}

} // namespace

MasterAudioEngine::MasterAudioEngine()
    : gainLinear_(std::make_shared<std::atomic<float>>(1.0f)) {
  graph_.setPlayConfigDetails(2, 2, sampleRate_, blockSize_);
  rebuildGraph();
}

MasterAudioEngine::~MasterAudioEngine() {
  alive_->store(false, std::memory_order_release);
  onChanged_ = nullptr;
  graph_.releaseResources();
  graph_.clear(kNoUpdate);
  inserts_.clear();
}

void MasterAudioEngine::prepareToPlay(double sampleRate, int blockSize) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
  blockSize_ = juce::jmax(1, blockSize);
  midiScratch_.ensureSize(4096);
  for (const auto &entry : inserts_)
    entry->hosted->prepareToPlay(sampleRate_, blockSize_);
  graph_.setPlayConfigDetails(2, 2, sampleRate_, blockSize_);
  graph_.prepareToPlay(sampleRate_, blockSize_);
  prepared_.store(true, std::memory_order_release);
  latencySamples_.store(graph_.getLatencySamples(), std::memory_order_release);
}

void MasterAudioEngine::releaseResources() {
  prepared_.store(false, std::memory_order_release);
  graph_.releaseResources();
}

void MasterAudioEngine::processBlock(juce::AudioBuffer<float> &audio) {
  if (!prepared_.load(std::memory_order_acquire) || audio.getNumChannels() < 2)
    return;

  juce::AudioBuffer<float> stereo(audio.getArrayOfWritePointers(), 2,
                                  audio.getNumSamples());
  midiScratch_.clear();
  graph_.processBlock(stereo, midiScratch_);

  for (int channel = 2; channel < audio.getNumChannels(); ++channel)
    audio.clear(channel, 0, audio.getNumSamples());

  const float peak =
      juce::jmax(stereo.getMagnitude(0, 0, stereo.getNumSamples()),
                 stereo.getMagnitude(1, 0, stereo.getNumSamples()));
  const float measured = juce::Decibels::gainToDecibels(peak, -120.0f);
  const float previous = peakDb_.load(std::memory_order_relaxed);
  const float decay = static_cast<float>(
      20.0 * static_cast<double>(stereo.getNumSamples()) / sampleRate_);
  peakDb_.store(juce::jmax(measured, previous - decay),
                std::memory_order_relaxed);
}

void MasterAudioEngine::setOnChanged(ChangeCallback callback) {
  onChanged_ = std::move(callback);
}

int MasterAudioEngine::insertCount() const noexcept {
  return static_cast<int>(inserts_.size());
}

int MasterAudioEngine::indexOf(const juce::String &slotId) const noexcept {
  for (int index = 0; index < static_cast<int>(inserts_.size()); ++index)
    if (inserts_[static_cast<std::size_t>(index)]->id == slotId)
      return index;
  return -1;
}

std::shared_ptr<MasterAudioEngine::Entry>
MasterAudioEngine::find(const juce::String &slotId) const {
  for (const auto &entry : inserts_)
    if (entry->id == slotId)
      return entry;
  return nullptr;
}

std::optional<MasterInsertSnapshot>
MasterAudioEngine::snapshot(const juce::String &slotId) const {
  const auto entry = find(slotId);
  if (!entry)
    return std::nullopt;

  MasterInsertSnapshot result;
  result.slotId = entry->id;
  result.description = entry->description;
  result.bypassed = entry->hosted->isBypassed();
  if (!entry->hosted->captureState(result.pluginState))
    result.pluginState = entry->hosted->cachedState();
  return result;
}

MasterAudioSnapshot MasterAudioEngine::snapshotAll() const {
  MasterAudioSnapshot result;
  result.gainDb = gainDb();
  result.inserts.reserve(inserts_.size());
  for (const auto &entry : inserts_) {
    if (const auto value = snapshot(entry->id))
      result.inserts.push_back(*value);
  }
  return result;
}

bool MasterAudioEngine::insert(const MasterInsertSnapshot &snapshot, int index,
                               juce::AudioPluginFormatManager &formatManager,
                               LoadCompletion completion, bool notify) {
  if (snapshot.slotId.isEmpty() || find(snapshot.slotId))
    return false;

  const auto compatibility = PluginCompatibility::fromDescription(
      snapshot.description, PluginSlotRole::effect);
  if (!compatibility.compatible) {
    if (completion)
      completion(false, compatibility.reason);
    return false;
  }

  auto entry = std::make_shared<Entry>();
  entry->id = snapshot.slotId;
  entry->description = snapshot.description;
  entry->hosted = std::make_shared<HostedPluginSlot>(PluginSlotRole::effect);
  entry->hosted->setId({snapshot.slotId});
  entry->hosted->setBypassed(snapshot.bypassed);

  index = juce::jlimit(0, static_cast<int>(inserts_.size()), index);
  inserts_.insert(inserts_.begin() + index, entry);
  rebuildGraph();
  notifyChanged(notify);

  const std::weak_ptr<std::atomic<bool>> weakAlive = alive_;
  entry->hosted->loadPlugin(
      snapshot.description, formatManager, sampleRate_, blockSize_,
      [this, weakAlive, entry, state = snapshot.pluginState,
       bypassed = snapshot.bypassed, completion = std::move(completion),
       notify](bool success, const juce::String &error) mutable {
        const auto alive = weakAlive.lock();
        if (!alive || !alive->load(std::memory_order_acquire))
          return;

        if (success) {
          if (!state.isEmpty())
            entry->hosted->applyState(state.getData(),
                                      static_cast<int>(state.getSize()));
          entry->hosted->setBypassed(bypassed);
        } else {
          entry->hosted->markMissing(entry->description, state, error);
          entry->hosted->setBypassed(bypassed);
        }
        entry->parameterFingerprint = entry->hosted->parameterFingerprint();

        // The action may have been undone while the async load was pending.
        if (find(entry->id) == entry) {
          rebuildGraph();
          notifyChanged(notify);
        }
        if (completion)
          completion(success, error);
      });
  return true;
}

bool MasterAudioEngine::insertProcessor(
    const MasterInsertSnapshot &snapshot, int index,
    std::unique_ptr<juce::AudioProcessor> processor, bool notify) {
  if (snapshot.slotId.isEmpty() || find(snapshot.slotId) || !processor)
    return false;

  auto entry = std::make_shared<Entry>();
  entry->id = snapshot.slotId;
  entry->description = snapshot.description;
  entry->hosted = std::make_shared<HostedPluginSlot>(PluginSlotRole::effect);
  entry->hosted->setId({snapshot.slotId});
  juce::String error;
  if (!entry->hosted->installProcessor(snapshot.description,
                                       std::move(processor), sampleRate_,
                                       blockSize_, error))
    return false;
  if (!snapshot.pluginState.isEmpty())
    entry->hosted->applyState(snapshot.pluginState.getData(),
                              static_cast<int>(snapshot.pluginState.getSize()));
  entry->hosted->setBypassed(snapshot.bypassed);
  entry->parameterFingerprint = entry->hosted->parameterFingerprint();

  index = juce::jlimit(0, static_cast<int>(inserts_.size()), index);
  inserts_.insert(inserts_.begin() + index, std::move(entry));
  rebuildGraph();
  notifyChanged(notify);
  return true;
}

bool MasterAudioEngine::remove(const juce::String &slotId, bool notify) {
  const auto iterator =
      std::find_if(inserts_.begin(), inserts_.end(),
                   [&](const auto &entry) { return entry->id == slotId; });
  if (iterator == inserts_.end())
    return false;
  (*iterator)->hosted->closeEditor();
  (*iterator)->hosted->unload();
  inserts_.erase(iterator);
  rebuildGraph();
  notifyChanged(notify);
  return true;
}

void MasterAudioEngine::clear(bool notify) {
  for (const auto &entry : inserts_) {
    entry->hosted->closeEditor();
    entry->hosted->unload();
  }
  inserts_.clear();
  setGainDb(0.0f, false);
  rebuildGraph();
  notifyChanged(notify);
}

bool MasterAudioEngine::move(const juce::String &slotId, int newIndex,
                             bool notify) {
  const int oldIndex = indexOf(slotId);
  if (oldIndex < 0 || inserts_.empty())
    return false;
  newIndex = juce::jlimit(0, static_cast<int>(inserts_.size()) - 1, newIndex);
  if (oldIndex == newIndex)
    return true;
  auto entry = inserts_[static_cast<std::size_t>(oldIndex)];
  inserts_.erase(inserts_.begin() + oldIndex);
  inserts_.insert(inserts_.begin() + newIndex, std::move(entry));
  rebuildGraph();
  notifyChanged(notify);
  return true;
}

bool MasterAudioEngine::setBypassed(const juce::String &slotId, bool bypassed,
                                    bool notify) {
  const auto entry = find(slotId);
  if (!entry)
    return false;
  entry->hosted->setBypassed(bypassed);
  notifyChanged(notify);
  return true;
}

bool MasterAudioEngine::showEditor(const juce::String &slotId) {
  const auto entry = find(slotId);
  if (!entry || !entry->hosted->hasProcessor())
    return false;
  entry->hosted->showEditor("Master - " + entry->description.name);
  return true;
}

void MasterAudioEngine::setGainDb(float gainDb, bool notify) {
  gainDb = juce::jlimit(-120.0f, 6.0f, gainDb);
  gainDb_.store(gainDb, std::memory_order_release);
  gainLinear_->store(juce::Decibels::decibelsToGain(gainDb, -120.0f),
                     std::memory_order_release);
  notifyChanged(notify);
}

float MasterAudioEngine::gainDb() const noexcept {
  return gainDb_.load(std::memory_order_acquire);
}

float MasterAudioEngine::peakDb() const noexcept {
  return peakDb_.load(std::memory_order_relaxed);
}

int MasterAudioEngine::latencySamples() const noexcept {
  return latencySamples_.load(std::memory_order_acquire);
}

double MasterAudioEngine::latencyMs() const noexcept {
  return sampleRate_ > 0.0
             ? 1000.0 * static_cast<double>(latencySamples()) / sampleRate_
             : 0.0;
}

bool MasterAudioEngine::consumePluginChanges(bool suppressPlaybackChanges) {
  bool changed = false;
  bool latencyChanged = false;
  for (const auto &entry : inserts_) {
    const bool explicitEdit =
        entry->hosted->consumeExplicitEditNotification();
    const bool nonParameterStateChanged =
        entry->hosted->consumeNonParameterStateChangeNotification();
    if (!entry->hosted->consumeChangeNotification() && !explicitEdit &&
        !nonParameterStateChanged)
      continue;

    const auto fingerprint = entry->hosted->parameterFingerprint();
    const bool parametersChanged =
        entry->parameterFingerprint &&
        *entry->parameterFingerprint != fingerprint;
    entry->parameterFingerprint = fingerprint;
    const int currentLatency =
        entry->hosted->activeProcessor()
            ? entry->hosted->activeProcessor()->getLatencySamples()
            : 0;
    latencyChanged |= entry->graphLatencySamples != currentLatency;

    if ((suppressPlaybackChanges && !explicitEdit) ||
        (!parametersChanged && !explicitEdit && !nonParameterStateChanged))
      continue;

    entry->hosted->refreshStateCache();
    changed = true;
  }
  if (latencyChanged)
    rebuildGraph();
  if (changed)
    notifyChanged(true);
  return changed;
}

bool MasterAudioEngine::refreshPluginStateCaches() {
  bool changed = false;
  for (const auto &entry : inserts_) {
    const auto fingerprint = entry->hosted->parameterFingerprint();
    if (!entry->parameterFingerprint) {
      entry->parameterFingerprint = fingerprint;
      continue;
    }
    if (*entry->parameterFingerprint == fingerprint)
      continue;
    entry->parameterFingerprint = fingerprint;
    entry->hosted->refreshStateCache();
    changed = true;
  }
  if (changed)
    notifyChanged(true);
  return changed;
}

void MasterAudioEngine::captureParameterFingerprints() {
  for (const auto &entry : inserts_)
    entry->parameterFingerprint = entry->hosted->parameterFingerprint();
}

juce::var MasterAudioEngine::toJson() const {
  auto *object = new juce::DynamicObject();
  object->setProperty("id", kMasterAudioId);
  object->setProperty("name", "Master");
  object->setProperty("gainDb", static_cast<double>(gainDb()));
  object->setProperty("peakDb", static_cast<double>(peakDb()));
  object->setProperty("latencySamples", latencySamples());
  object->setProperty("latencyMs", latencyMs());

  juce::Array<juce::var> inserts;
  for (int index = 0; index < static_cast<int>(inserts_.size()); ++index) {
    const auto &entry = inserts_[static_cast<std::size_t>(index)];
    auto *slot = new juce::DynamicObject();
    slot->setProperty("id", entry->id);
    slot->setProperty("position", index);
    slot->setProperty("pluginUid", entry->description.uniqueId);
    slot->setProperty("name", entry->description.name);
    slot->setProperty("manufacturer", entry->description.manufacturerName);
    slot->setProperty("format", entry->description.pluginFormatName);
    slot->setProperty("bypassed", entry->hosted->isBypassed());
    slot->setProperty("status",
                      HostedPluginSlot::statusName(entry->hosted->status()));
    slot->setProperty("error", entry->hosted->lastError());
    slot->setProperty(
        "latencySamples",
        entry->hosted->activeProcessor()
            ? entry->hosted->activeProcessor()->getLatencySamples()
            : 0);
    inserts.add(juce::var(slot));
  }
  object->setProperty("inserts", juce::var(inserts));
  return juce::var(object);
}

void MasterAudioEngine::rebuildGraph() {
  graph_.clear(kNoUpdate);

  auto previous = graph_.addNode(
      std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode),
      std::nullopt, kNoUpdate);

  for (const auto &entry : inserts_) {
    entry->graphLatencySamples =
        entry->hosted->activeProcessor()
            ? entry->hosted->activeProcessor()->getLatencySamples()
            : 0;
    auto node = graph_.addNode(
        std::make_unique<HostedEffectGraphProcessor>(entry->hosted),
        std::nullopt, kNoUpdate);
    connectStereo(graph_, previous, node);
    previous = std::move(node);
  }

  auto gainNode =
      graph_.addNode(std::make_unique<MasterGainProcessor>(gainLinear_),
                     std::nullopt, kNoUpdate);
  connectStereo(graph_, previous, gainNode);

  auto output = graph_.addNode(
      std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode),
      std::nullopt, kNoUpdate);
  connectStereo(graph_, gainNode, output);

  graph_.rebuild();
  latencySamples_.store(graph_.getLatencySamples(), std::memory_order_release);
}

void MasterAudioEngine::notifyChanged(bool notify) {
  if (notify && onChanged_)
    onChanged_();
}

} // namespace fiddle
