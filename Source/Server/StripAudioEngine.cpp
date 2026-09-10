#include "StripAudioEngine.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace fiddle {
void StripAudioEngine::appendPluginTimings(juce::Array<juce::var> &rows,
                                           const juce::String &owner, double now) {
  for (const auto &entry : preFaderInserts_)
    entry->hosted->appendTiming(rows, owner, "Pre-fader FX", now);
  for (const auto &entry : postFaderInserts_)
    entry->hosted->appendTiming(rows, owner, "Post-fader FX", now);
}
namespace {

constexpr auto kNoUpdate = juce::AudioProcessorGraph::UpdateKind::none;

class StripGainProcessor final : public juce::AudioProcessor {
public:
  explicit StripGainProcessor(std::shared_ptr<std::atomic<float>> gain)
      : juce::AudioProcessor(
            BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        gain_(std::move(gain)) {}

  const juce::String getName() const override { return "Strip Gain"; }
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

class HostedStripEffectProcessor final : public juce::AudioProcessor {
public:
  explicit HostedStripEffectProcessor(std::shared_ptr<HostedPluginSlot> slot)
      : juce::AudioProcessor(
            BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        slot_(std::move(slot)) {
    if (auto *processor = slot_->activeProcessor())
      setLatencySamples(processor->getLatencySamples());
  }

  const juce::String getName() const override { return "Hosted Strip Effect"; }
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

juce::var insertToJson(const AudioInsertSnapshot &insert) {
  auto *object = new juce::DynamicObject();
  object->setProperty("slotId", insert.slotId);
  object->setProperty("formatName", insert.description.pluginFormatName);
  object->setProperty("pluginUid", insert.description.uniqueId);
  object->setProperty("fileOrIdentifier", insert.description.fileOrIdentifier);
  object->setProperty("manufacturer", insert.description.manufacturerName);
  object->setProperty("name", insert.description.name);
  object->setProperty("category", insert.description.category);
  object->setProperty("pluginVersion", insert.description.version);
  object->setProperty("numInputChannels", insert.description.numInputChannels);
  object->setProperty("numOutputChannels", insert.description.numOutputChannels);
  object->setProperty("bypassed", insert.bypassed);
  object->setProperty("pluginState", insert.pluginState.toBase64Encoding());
  return juce::var(object);
}

AudioInsertSnapshot insertFromJson(const juce::var &value) {
  AudioInsertSnapshot insert;
  const auto *object = value.getDynamicObject();
  if (!object)
    return insert;
  insert.slotId = object->getProperty("slotId").toString();
  insert.description.pluginFormatName =
      object->getProperty("formatName").toString();
  insert.description.uniqueId =
      static_cast<int>(object->getProperty("pluginUid"));
  insert.description.fileOrIdentifier =
      object->getProperty("fileOrIdentifier").toString();
  insert.description.manufacturerName =
      object->getProperty("manufacturer").toString();
  insert.description.name = object->getProperty("name").toString();
  insert.description.category = object->getProperty("category").toString();
  insert.description.version =
      object->getProperty("pluginVersion").toString();
  insert.description.numInputChannels =
      static_cast<int>(object->getProperty("numInputChannels"));
  insert.description.numOutputChannels =
      static_cast<int>(object->getProperty("numOutputChannels"));
  insert.description.isInstrument = false;
  insert.bypassed = static_cast<bool>(object->getProperty("bypassed"));
  insert.pluginState.fromBase64Encoding(
      object->getProperty("pluginState").toString());
  return insert;
}

} // namespace

juce::MemoryBlock serializeStripAudioSnapshot(const StripAudioSnapshot &state) {
  auto *root = new juce::DynamicObject();
  root->setProperty("version", 1);
  juce::Array<juce::var> pre;
  for (const auto &insert : state.preFaderInserts)
    pre.add(insertToJson(insert));
  root->setProperty("preFader", juce::var(pre));
  juce::Array<juce::var> post;
  for (const auto &insert : state.postFaderInserts)
    post.add(insertToJson(insert));
  root->setProperty("postFader", juce::var(post));
  const auto json = juce::JSON::toString(juce::var(root), false).toStdString();
  return juce::MemoryBlock(json.data(), json.size());
}

StripAudioSnapshot deserializeStripAudioSnapshot(const void *data,
                                                 std::size_t size) {
  StripAudioSnapshot result;
  if (!data || size == 0)
    return result;
  const auto parsed = juce::JSON::parse(
      juce::String::fromUTF8(static_cast<const char *>(data),
                             static_cast<int>(size)));
  const auto *root = parsed.getDynamicObject();
  if (!root || static_cast<int>(root->getProperty("version")) != 1)
    return result;
  const auto append = [](const juce::var &value,
                         std::vector<AudioInsertSnapshot> &destination) {
    const auto *array = value.getArray();
    if (!array)
      return;
    for (const auto &item : *array) {
      auto insert = insertFromJson(item);
      if (insert.slotId.isNotEmpty())
        destination.push_back(std::move(insert));
    }
  };
  append(root->getProperty("preFader"), result.preFaderInserts);
  append(root->getProperty("postFader"), result.postFaderInserts);
  return result;
}

StripAudioEngine::StripAudioEngine()
    : gainLinear_(std::make_shared<std::atomic<float>>(1.0f)) {
  graph_.setPlayConfigDetails(2, 2, sampleRate_, blockSize_);
  rebuildGraph();
}

StripAudioEngine::~StripAudioEngine() {
  alive_->store(false, std::memory_order_release);
  onChanged_ = nullptr;
  prepared_.store(false, std::memory_order_release);
  graph_.releaseResources();
  graph_.clear(kNoUpdate);
  preFaderInserts_.clear();
  postFaderInserts_.clear();
}

void StripAudioEngine::prepareToPlay(double sampleRate, int blockSize) {
  AudioProcessingGate::Control control;
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
  blockSize_ = juce::jmax(1, blockSize);
  midiScratch_.ensureSize(1024);
  for (const auto position : {StripInsertPosition::preFader,
                              StripInsertPosition::postFader})
    for (const auto &entry : rack(position))
      entry->hosted->prepareToPlay(sampleRate_, blockSize_);
  graph_.setPlayConfigDetails(2, 2, sampleRate_, blockSize_);
  graph_.prepareToPlay(sampleRate_, blockSize_);
  prepared_.store(true, std::memory_order_release);
  latencySamples_.store(graph_.getLatencySamples(), std::memory_order_release);
}

void StripAudioEngine::releaseResources() {
  AudioProcessingGate::Control control;
  prepared_.store(false, std::memory_order_release);
  graph_.releaseResources();
}

void StripAudioEngine::processBlock(juce::AudioBuffer<float> &audio,
                                    float effectiveGain) {
  AudioProcessingGate::Render render;
  if (!render) return;
  gainLinear_->store(effectiveGain, std::memory_order_release);
  if (!prepared_.load(std::memory_order_acquire) ||
      audio.getNumChannels() < 2)
    return;

  juce::AudioBuffer<float> stereo(audio.getArrayOfWritePointers(), 2,
                                  audio.getNumSamples());
  midiScratch_.clear();
  graph_.processBlock(stereo, midiScratch_);

  for (int channel = 2; channel < audio.getNumChannels(); ++channel)
    audio.clear(channel, 0, audio.getNumSamples());
}

void StripAudioEngine::setOnChanged(ChangeCallback callback) {
  onChanged_ = std::move(callback);
}

void StripAudioEngine::setOnEditorVisibilityChanged(ChangeCallback callback) {
  onEditorVisibilityChanged_ = std::move(callback);
}

StripAudioEngine::Rack &
StripAudioEngine::rack(StripInsertPosition position) noexcept {
  return position == StripInsertPosition::preFader ? preFaderInserts_
                                                   : postFaderInserts_;
}

const StripAudioEngine::Rack &
StripAudioEngine::rack(StripInsertPosition position) const noexcept {
  return position == StripInsertPosition::preFader ? preFaderInserts_
                                                   : postFaderInserts_;
}

int StripAudioEngine::insertCount(StripInsertPosition position) const noexcept {
  return static_cast<int>(rack(position).size());
}

int StripAudioEngine::indexOf(const juce::String &slotId,
                              StripInsertPosition position) const noexcept {
  const auto &entries = rack(position);
  for (int index = 0; index < static_cast<int>(entries.size()); ++index)
    if (entries[static_cast<std::size_t>(index)]->id == slotId)
      return index;
  return -1;
}

std::shared_ptr<StripAudioEngine::Entry>
StripAudioEngine::find(const juce::String &slotId) const {
  for (const auto position : {StripInsertPosition::preFader,
                              StripInsertPosition::postFader})
    for (const auto &entry : rack(position))
      if (entry->id == slotId)
        return entry;
  return nullptr;
}

std::optional<StripInsertPosition>
StripAudioEngine::positionOf(const juce::String &slotId) const {
  if (indexOf(slotId, StripInsertPosition::preFader) >= 0)
    return StripInsertPosition::preFader;
  if (indexOf(slotId, StripInsertPosition::postFader) >= 0)
    return StripInsertPosition::postFader;
  return std::nullopt;
}

std::optional<AudioInsertSnapshot>
StripAudioEngine::snapshot(const juce::String &slotId, bool captureLiveState) const {
  const auto entry = find(slotId);
  if (!entry)
    return std::nullopt;
  AudioInsertSnapshot result;
  result.slotId = entry->id;
  result.description = entry->description;
  result.bypassed = entry->hosted->isBypassed();
  if (!captureLiveState || !entry->hosted->captureState(result.pluginState))
    result.pluginState = entry->hosted->cachedState();
  return result;
}

StripAudioSnapshot StripAudioEngine::snapshotAll(bool captureLiveState) const {
  StripAudioSnapshot result;
  const auto append = [this, captureLiveState](const Rack &entries,
                             std::vector<AudioInsertSnapshot> &destination) {
    destination.reserve(entries.size());
    for (const auto &entry : entries)
      if (const auto value = snapshot(entry->id, captureLiveState))
        destination.push_back(*value);
  };
  append(preFaderInserts_, result.preFaderInserts);
  append(postFaderInserts_, result.postFaderInserts);
  return result;
}

bool StripAudioEngine::insert(const AudioInsertSnapshot &snapshot,
                              StripInsertPosition position, int index,
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
  auto &entries = rack(position);
  index = juce::jlimit(0, static_cast<int>(entries.size()), index);
  entries.insert(entries.begin() + index, entry);
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
        if (find(entry->id) == entry) {
          rebuildGraph();
          notifyChanged(notify);
        }
        if (completion)
          completion(success, error);
      });
  return true;
}

bool StripAudioEngine::insertProcessor(
    const AudioInsertSnapshot &snapshot, StripInsertPosition position,
    int index, std::unique_ptr<juce::AudioProcessor> processor, bool notify) {
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
  auto &entries = rack(position);
  index = juce::jlimit(0, static_cast<int>(entries.size()), index);
  entries.insert(entries.begin() + index, std::move(entry));
  rebuildGraph();
  notifyChanged(notify);
  return true;
}

bool StripAudioEngine::remove(const juce::String &slotId, bool notify) {
  const auto position = positionOf(slotId);
  if (!position)
    return false;
  auto &entries = rack(*position);
  const auto iterator = std::find_if(
      entries.begin(), entries.end(),
      [&](const auto &entry) { return entry->id == slotId; });
  (*iterator)->hosted->closeEditor();
  (*iterator)->hosted->unload();
  entries.erase(iterator);
  rebuildGraph();
  notifyChanged(notify);
  return true;
}

void StripAudioEngine::clear(bool notify) {
  for (const auto position : {StripInsertPosition::preFader,
                              StripInsertPosition::postFader}) {
    for (const auto &entry : rack(position)) {
      entry->hosted->closeEditor();
      entry->hosted->unload();
    }
    rack(position).clear();
  }
  rebuildGraph();
  notifyChanged(notify);
}

bool StripAudioEngine::move(const juce::String &slotId,
                            StripInsertPosition newPosition, int newIndex,
                            bool notify) {
  const auto oldPosition = positionOf(slotId);
  if (!oldPosition)
    return false;
  auto &source = rack(*oldPosition);
  const int oldIndex = indexOf(slotId, *oldPosition);
  auto entry = source[static_cast<std::size_t>(oldIndex)];
  source.erase(source.begin() + oldIndex);
  auto &destination = rack(newPosition);
  newIndex = juce::jlimit(0, static_cast<int>(destination.size()), newIndex);
  destination.insert(destination.begin() + newIndex, std::move(entry));
  rebuildGraph();
  notifyChanged(notify);
  return true;
}

bool StripAudioEngine::setBypassed(const juce::String &slotId, bool bypassed,
                                   bool notify) {
  const auto entry = find(slotId);
  if (!entry)
    return false;
  entry->hosted->setBypassed(bypassed);
  notifyChanged(notify);
  return true;
}

bool StripAudioEngine::showEditor(const juce::String &slotId,
                                  const juce::String &stripName) {
  const auto entry = find(slotId);
  if (!entry || !entry->hosted->hasProcessor())
    return false;
  const std::weak_ptr<std::atomic<bool>> weakAlive = alive_;
  entry->hosted->showEditor(
      stripName + " - " + entry->description.name, [this, weakAlive] {
        const auto alive = weakAlive.lock();
        if (alive && alive->load(std::memory_order_acquire) &&
            onEditorVisibilityChanged_)
          onEditorVisibilityChanged_();
      });
  return true;
}

bool StripAudioEngine::toggleEditor(const juce::String &slotId,
                                    const juce::String &stripName) {
  const auto entry = find(slotId);
  if (!entry || !entry->hosted->hasProcessor())
    return false;
  if (entry->hosted->isEditorVisible())
    entry->hosted->closeEditor();
  else
    return showEditor(slotId, stripName);
  return true;
}

bool StripAudioEngine::consumePluginChanges(bool suppressPlaybackChanges) {
  bool changed = false;
  bool latencyChanged = false;
  for (const auto position : {StripInsertPosition::preFader,
                              StripInsertPosition::postFader}) {
    for (const auto &entry : rack(position)) {
      const bool explicitEdit =
          entry->hosted->consumeExplicitEditNotification();
      const bool nonParameterStateChanged =
          entry->hosted->consumeNonParameterStateChangeNotification();
      if (!entry->hosted->consumeChangeNotification() && !explicitEdit &&
          !nonParameterStateChanged)
        continue;
      const auto fingerprint = entry->hosted->parameterFingerprint();
      const bool parametersChanged = entry->parameterFingerprint &&
                                     *entry->parameterFingerprint != fingerprint;
      entry->parameterFingerprint = fingerprint;
      const int currentLatency = entry->hosted->activeProcessor()
                                     ? entry->hosted->activeProcessor()
                                           ->getLatencySamples()
                                     : 0;
      latencyChanged |= entry->graphLatencySamples != currentLatency;
      if ((suppressPlaybackChanges && !explicitEdit) ||
          (!parametersChanged && !explicitEdit && !nonParameterStateChanged))
        continue;
      entry->hosted->refreshStateCache();
      changed = true;
    }
  }
  if (latencyChanged) {
    rebuildGraph();
    latencyDisplayChanged_ = true;
  }
  if (changed)
    notifyChanged(true);
  return changed;
}

bool StripAudioEngine::refreshPluginStateCaches() {
  bool changed = false;
  for (const auto position : {StripInsertPosition::preFader,
                              StripInsertPosition::postFader}) {
    for (const auto &entry : rack(position)) {
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
  }
  if (changed)
    notifyChanged(true);
  return changed;
}

void StripAudioEngine::captureParameterFingerprints() {
  for (const auto position : {StripInsertPosition::preFader,
                              StripInsertPosition::postFader})
    for (const auto &entry : rack(position))
      entry->parameterFingerprint = entry->hosted->parameterFingerprint();
}

juce::var StripAudioEngine::toJson() const {
  auto *object = new juce::DynamicObject();
  object->setProperty("latencySamples", latencySamples());
  object->setProperty("latencyMs", latencyMs());
  const auto append = [](const Rack &entries, StripInsertPosition position) {
    juce::Array<juce::var> result;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
      const auto &entry = entries[static_cast<std::size_t>(index)];
      auto *slot = new juce::DynamicObject();
      slot->setProperty("id", entry->id);
      slot->setProperty("position", index);
      slot->setProperty("section", position == StripInsertPosition::preFader
                                               ? "preFader"
                                               : "postFader");
      slot->setProperty("pluginUid", entry->description.uniqueId);
      slot->setProperty("name", entry->description.name);
      slot->setProperty("manufacturer", entry->description.manufacturerName);
      slot->setProperty("format", entry->description.pluginFormatName);
      slot->setProperty("bypassed", entry->hosted->isBypassed());
      slot->setProperty("editorOpen", entry->hosted->isEditorVisible());
      slot->setProperty("status",
                        HostedPluginSlot::statusName(entry->hosted->status()));
      slot->setProperty("error", entry->hosted->lastError());
      result.add(juce::var(slot));
    }
    return juce::var(result);
  };
  object->setProperty("preFaderInserts",
                      append(preFaderInserts_, StripInsertPosition::preFader));
  object->setProperty(
      "postFaderInserts",
      append(postFaderInserts_, StripInsertPosition::postFader));
  object->setProperty(
      "insertCount",
      static_cast<int>(preFaderInserts_.size() + postFaderInserts_.size()));
  return juce::var(object);
}

int StripAudioEngine::latencySamples() const noexcept {
  return latencySamples_.load(std::memory_order_acquire);
}

double StripAudioEngine::latencyMs() const noexcept {
  return sampleRate_ > 0.0
             ? 1000.0 * static_cast<double>(latencySamples()) / sampleRate_
             : 0.0;
}

void StripAudioEngine::rebuildGraph() {
  graph_.clear(kNoUpdate);
  auto previous = graph_.addNode(
      std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode),
      std::nullopt, kNoUpdate);

  const auto appendRack = [this, &previous](const Rack &entries) {
    for (const auto &entry : entries) {
      entry->graphLatencySamples =
          entry->hosted->activeProcessor()
              ? entry->hosted->activeProcessor()->getLatencySamples()
              : 0;
      auto node = graph_.addNode(
          std::make_unique<HostedStripEffectProcessor>(entry->hosted),
          std::nullopt, kNoUpdate);
      connectStereo(graph_, previous, node);
      previous = std::move(node);
    }
  };
  appendRack(preFaderInserts_);

  auto gain = graph_.addNode(std::make_unique<StripGainProcessor>(gainLinear_),
                             std::nullopt, kNoUpdate);
  connectStereo(graph_, previous, gain);
  previous = std::move(gain);

  appendRack(postFaderInserts_);
  auto output = graph_.addNode(
      std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode),
      std::nullopt, kNoUpdate);
  connectStereo(graph_, previous, output);
  graph_.rebuild();
  latencySamples_.store(graph_.getLatencySamples(), std::memory_order_release);
}

void StripAudioEngine::notifyChanged(bool notify) {
  if (notify && onChanged_)
    onChanged_();
}

} // namespace fiddle
