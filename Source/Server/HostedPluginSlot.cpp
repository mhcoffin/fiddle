#include "HostedPluginSlot.h"

#include "PluginEditorWindow.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace fiddle {

PluginSlotId PluginSlotId::instrumentFor(const juce::String &stripId) {
  return {stripId + ":instrument"};
}

PluginCompatibility
PluginCompatibility::fromDescription(const juce::PluginDescription &description,
                                     PluginSlotRole role) {
  PluginCompatibility result;
  result.isInstrument = description.isInstrument;
  result.hasStereoInput = description.numInputChannels >= 2;
  result.hasStereoOutput = description.numOutputChannels >= 2;
  const bool inputLayoutUnknown = description.numInputChannels == 0;
  const bool outputLayoutUnknown = description.numOutputChannels == 0;

  if (role == PluginSlotRole::instrument) {
    result.compatible =
        result.isInstrument && (result.hasStereoOutput || outputLayoutUnknown);
    result.requiresRuntimeValidation = result.compatible && outputLayoutUnknown;
    if (!result.isInstrument)
      result.reason = "Plug-in is not reported as an instrument";
    else if (!result.hasStereoOutput && !outputLayoutUnknown)
      result.reason = "Instrument has no stereo output";
  } else {
    result.compatible = !result.isInstrument &&
                        (result.hasStereoInput || inputLayoutUnknown) &&
                        (result.hasStereoOutput || outputLayoutUnknown);
    result.requiresRuntimeValidation =
        result.compatible && (inputLayoutUnknown || outputLayoutUnknown);
    if (result.isInstrument)
      result.reason = "Instrument plug-ins cannot occupy effect slots";
    else if (!result.hasStereoInput && !inputLayoutUnknown)
      result.reason = "Effect has no stereo input";
    else if (!result.hasStereoOutput && !outputLayoutUnknown)
      result.reason = "Effect has no stereo output";
  }

  return result;
}

juce::var PluginCompatibility::toJson() const {
  auto *object = new juce::DynamicObject();
  object->setProperty("isInstrument", isInstrument);
  object->setProperty("hasStereoInput", hasStereoInput);
  object->setProperty("hasStereoOutput", hasStereoOutput);
  object->setProperty("compatible", compatible);
  object->setProperty("requiresRuntimeValidation", requiresRuntimeValidation);
  object->setProperty("reason", reason);
  return juce::var(object);
}

HostedPluginSlot::HostedPluginSlot(PluginSlotRole role) : role_(role) {}

HostedPluginSlot::~HostedPluginSlot() {
  alive_->store(false, std::memory_order_release);
  closeEditor();
  listenerProcessor_.store(nullptr, std::memory_order_release);
  publishRuntime(nullptr);
  jassert(runtime_.readerCount() == 0);
  reclaimRetiredRuntimes();
}

void HostedPluginSlot::setId(PluginSlotId id) { id_ = std::move(id); }

juce::String HostedPluginSlot::statusName(HostedPluginStatus status) {
  switch (status) {
  case HostedPluginStatus::empty:
    return "empty";
  case HostedPluginStatus::loading:
    return "loading";
  case HostedPluginStatus::loaded:
    return "loaded";
  case HostedPluginStatus::missing:
    return "missing";
  case HostedPluginStatus::failed:
    return "failed";
  }
  jassertfalse;
  return "unknown";
}

bool HostedPluginSlot::hasProcessor() const noexcept {
  return messageThreadProcessor_ != nullptr;
}

void HostedPluginSlot::loadPlugin(const juce::PluginDescription &description,
                                  juce::AudioPluginFormatManager &formatManager,
                                  double sampleRate, int blockSize,
                                  LoadCompletion onComplete) {
  const auto generation = ++loadGeneration_;
  const std::weak_ptr<std::atomic<bool>> weakAlive = alive_;
  status_ = HostedPluginStatus::loading;
  lastError_.clear();

  formatManager.createPluginInstanceAsync(
      description, sampleRate, blockSize,
      [this, description, sampleRate, blockSize, generation, weakAlive,
       onComplete = std::move(onComplete)](
          std::unique_ptr<juce::AudioPluginInstance> instance,
          const juce::String &creationError) mutable {
        const auto alive = weakAlive.lock();
        if (!alive || !alive->load(std::memory_order_acquire) ||
            generation != loadGeneration_)
          return;

        if (!instance) {
          if (messageThreadProcessor_ == nullptr)
            markMissing(description, cachedState_, creationError);
          else
            status_ = HostedPluginStatus::loaded;
          lastError_ = creationError;
          if (onComplete)
            onComplete(false, creationError);
          return;
        }

        auto actualDescription = description;
        instance->fillInPluginDescription(actualDescription);
        juce::String error;
        const bool installed =
            installProcessor(actualDescription, std::move(instance), sampleRate,
                             blockSize, error);
        if (onComplete)
          onComplete(installed, error);
      });
}

bool HostedPluginSlot::installProcessor(
    const juce::PluginDescription &description,
    std::unique_ptr<juce::AudioProcessor> processor, double sampleRate,
    int blockSize, juce::String &error) {
  ++loadGeneration_;
  error.clear();
  const bool replacingActiveProcessor = hasProcessor();
  const auto declaredCompatibility =
      PluginCompatibility::fromDescription(description, role_);
  const auto fail = [&]() {
    lastError_ = error;
    if (!replacingActiveProcessor) {
      status_ = HostedPluginStatus::failed;
      description_ = description;
      compatibility_ = declaredCompatibility;
      pluginUid_ = description.uniqueId;
    } else {
      status_ = HostedPluginStatus::loaded;
    }
    return false;
  };

  if (!processor) {
    error = "No processor instance was supplied";
    return fail();
  }

  if (!declaredCompatibility.compatible) {
    error = declaredCompatibility.reason;
    return fail();
  }

  if (!configureBusLayout(*processor, error)) {
    return fail();
  }

  closeEditor();

  const int channels = juce::jmax(processor->getTotalNumInputChannels(),
                                  processor->getTotalNumOutputChannels(), 2);
  auto runtime = std::make_unique<Runtime>();
  runtime->scratchBuffer.setSize(channels, blockSize);
  processor->setNonRealtime(false);
  processor->prepareToPlay(sampleRate, blockSize);
  processor->addListener(&changeListener_);
  runtime->processor = std::move(processor);

  messageThreadProcessor_ = runtime->processor.get();
  listenerProcessor_.store(messageThreadProcessor_, std::memory_order_release);
  description_ = description;
  compatibility_ = declaredCompatibility;
  pluginUid_ = description.uniqueId;
  status_ = HostedPluginStatus::loaded;
  lastError_.clear();
  missingState_.reset();
  bypassed_.store(false, std::memory_order_relaxed);
  changeNotificationPending_.store(false, std::memory_order_relaxed);
  explicitEditNotificationPending_.store(false, std::memory_order_relaxed);
  nonParameterStateChangePending_.store(false, std::memory_order_relaxed);
  publishRuntime(std::move(runtime));
  refreshStateCache();
  return true;
}

void HostedPluginSlot::markMissing(const juce::PluginDescription &description,
                                   const juce::MemoryBlock &state,
                                   const juce::String &error) {
  ++loadGeneration_;
  closeEditor();
  listenerProcessor_.store(nullptr, std::memory_order_release);
  messageThreadProcessor_ = nullptr;
  publishRuntime(nullptr);

  description_ = description;
  compatibility_ = PluginCompatibility::fromDescription(description, role_);
  pluginUid_ = description.uniqueId;
  cachedState_ = state;
  lastError_ = error;
  missingState_ = MissingPluginState{description, state, error};
  status_ = HostedPluginStatus::missing;
}

void HostedPluginSlot::unload() {
  ++loadGeneration_;
  closeEditor();
  listenerProcessor_.store(nullptr, std::memory_order_release);
  messageThreadProcessor_ = nullptr;
  publishRuntime(nullptr);
  description_ = {};
  compatibility_ = {};
  pluginUid_ = 0;
  cachedState_.reset();
  lastError_.clear();
  missingState_.reset();
  status_ = HostedPluginStatus::empty;
  bypassed_.store(false, std::memory_order_relaxed);
  changeNotificationPending_.store(false, std::memory_order_relaxed);
  explicitEditNotificationPending_.store(false, std::memory_order_relaxed);
}

void HostedPluginSlot::reclaimRetiredRuntimes() {
  runtime_.reclaimRetired([this](Runtime &runtime) {
    runtime.processor->removeListener(&changeListener_);
    runtime.processor->releaseResources();
  });
}

void HostedPluginSlot::prepareToPlay(double sampleRate, int blockSize) {
  if (auto *runtime = runtime_.activeForWriter()) {
    const int channels =
        juce::jmax(runtime->processor->getTotalNumInputChannels(),
                   runtime->processor->getTotalNumOutputChannels(), 2);
    runtime->scratchBuffer.setSize(channels, blockSize);
    runtime->processor->prepareToPlay(sampleRate, blockSize);
  }
}

HostedPluginSlot::RuntimeRead HostedPluginSlot::readRuntime() const noexcept {
  return runtime_.read();
}

juce::AudioProcessor *HostedPluginSlot::activeProcessor() const noexcept {
  return messageThreadProcessor_;
}

bool HostedPluginSlot::processBlock(juce::AudioBuffer<float> &audio,
                                    juce::MidiBuffer &midi) {
  auto runtimeRead = readRuntime();
  auto *runtime = runtimeRead.get();
  if (runtime == nullptr)
    return false;

  if (isBypassed()) {
    if (role_ == PluginSlotRole::instrument) {
      audio.clear();
      midi.clear();
    }
    return true;
  }

  runtime->processor->processBlock(audio, midi);
  return true;
}

juce::MemoryBlock HostedPluginSlot::cachedState() const { return cachedState_; }

bool HostedPluginSlot::captureState(juce::MemoryBlock &destination) const {
  destination.reset();
  if (!messageThreadProcessor_)
    return false;
  messageThreadProcessor_->getStateInformation(destination);
  return true;
}

bool HostedPluginSlot::applyState(const void *data, int sizeInBytes) {
  if (!messageThreadProcessor_ || data == nullptr || sizeInBytes <= 0)
    return false;
  messageThreadProcessor_->setStateInformation(data, sizeInBytes);
  refreshStateCache();
  return true;
}

bool HostedPluginSlot::setProgram(int programIndex) {
  if (!messageThreadProcessor_)
    return false;
  messageThreadProcessor_->setCurrentProgram(programIndex);
  refreshStateCache();
  return true;
}

void HostedPluginSlot::refreshStateCache() {
  if (messageThreadProcessor_) {
    cachedState_.reset();
    messageThreadProcessor_->getStateInformation(cachedState_);
  } else if (!missingState_) {
    cachedState_.reset();
  }
}

uint64_t HostedPluginSlot::parameterFingerprint() const {
  if (!messageThreadProcessor_)
    return 0;

  constexpr uint64_t offsetBasis = 14695981039346656037ULL;
  constexpr uint64_t prime = 1099511628211ULL;
  uint64_t hash = offsetBasis;
  const auto appendBytes = [&](const void *data, std::size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (std::size_t index = 0; index < size; ++index) {
      hash ^= bytes[index];
      hash *= prime;
    }
  };

  const int program = messageThreadProcessor_->getCurrentProgram();
  appendBytes(&program, sizeof(program));
  const auto &parameters = messageThreadProcessor_->getParameters();
  for (int index = 0; index < parameters.size(); ++index) {
    auto *parameter = parameters[index];
    if (!parameter || !parameter->isAutomatable())
      continue;
    appendBytes(&index, sizeof(index));
    float value = parameter->getValue();
    if (!std::isfinite(value))
      value = 0.0f;
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendBytes(&bits, sizeof(bits));
  }
  return hash;
}

void HostedPluginSlot::setBypassed(bool bypassed) noexcept {
  bypassed_.store(bypassed, std::memory_order_relaxed);
}

bool HostedPluginSlot::isBypassed() const noexcept {
  return bypassed_.load(std::memory_order_relaxed);
}

void HostedPluginSlot::showEditor(const juce::String &title) {
  if (!messageThreadProcessor_)
    return;
  if (editorWindow_) {
    editorWindow_->setVisible(true);
    editorWindow_->toFront(true);
  } else if (auto *editor =
                 messageThreadProcessor_->createEditorAndMakeActive()) {
    editorWindow_ = std::make_unique<PluginEditorWindow>(title, editor);
  }
}

void HostedPluginSlot::closeEditor() { editorWindow_.reset(); }

bool HostedPluginSlot::consumeChangeNotification() noexcept {
  return changeNotificationPending_.exchange(false, std::memory_order_acq_rel);
}

bool HostedPluginSlot::consumeExplicitEditNotification() noexcept {
  return explicitEditNotificationPending_.exchange(false,
                                                    std::memory_order_acq_rel);
}

bool HostedPluginSlot::consumeNonParameterStateChangeNotification() noexcept {
  return nonParameterStateChangePending_.exchange(false,
                                                  std::memory_order_acq_rel);
}

void HostedPluginSlot::ChangeListener::audioProcessorParameterChanged(
    juce::AudioProcessor *processor, int, float) {
  owner_.noteProcessorChange(processor);
}

void HostedPluginSlot::ChangeListener::audioProcessorChanged(
    juce::AudioProcessor *processor,
    const juce::AudioProcessorListener::ChangeDetails &details) {
  if (details.nonParameterStateChanged)
    owner_.noteNonParameterStateChange(processor);
}

void HostedPluginSlot::ChangeListener::audioProcessorParameterChangeGestureEnd(
    juce::AudioProcessor *processor, int) {
  owner_.noteExplicitProcessorEdit(processor);
}

void HostedPluginSlot::noteProcessorChange(
    juce::AudioProcessor *processor) noexcept {
  if (processor == listenerProcessor_.load(std::memory_order_acquire))
    changeNotificationPending_.store(true, std::memory_order_release);
}

void HostedPluginSlot::noteExplicitProcessorEdit(
    juce::AudioProcessor *processor) noexcept {
  if (processor != listenerProcessor_.load(std::memory_order_acquire))
    return;
  explicitEditNotificationPending_.store(true, std::memory_order_release);
  changeNotificationPending_.store(true, std::memory_order_release);
}

void HostedPluginSlot::noteNonParameterStateChange(
    juce::AudioProcessor *processor) noexcept {
  if (processor != listenerProcessor_.load(std::memory_order_acquire))
    return;
  nonParameterStateChangePending_.store(true, std::memory_order_release);
  changeNotificationPending_.store(true, std::memory_order_release);
}

bool HostedPluginSlot::configureBusLayout(juce::AudioProcessor &processor,
                                          juce::String &error) const {
  const int inputBuses = processor.getBusCount(true);
  const int outputBuses = processor.getBusCount(false);
  if (outputBuses == 0) {
    error = "Plug-in exposes no audio output bus";
    return false;
  }
  if (role_ == PluginSlotRole::effect && inputBuses == 0) {
    error = "Effect exposes no audio input bus";
    return false;
  }

  auto layout = processor.getBusesLayout();

  // Existing instrument hosting has always supplied storage for every active
  // output channel and mixed only the primary stereo pair.  Keep the plug-in's
  // native layout intact here.  In particular, some multi-output VST3
  // instruments retain their internal bus arrangement after setBusesLayout()
  // and will write to the old outputs even when the host has disabled them.
  // That leaves JUCE with null channel pointers and can crash the audio thread.
  if (role_ == PluginSlotRole::instrument) {
    if (layout.getMainOutputChannelSet().size() < 2) {
      error = "Instrument exposes no usable primary stereo output";
      return false;
    }
    return true;
  }

  for (int bus = 0; bus < layout.inputBuses.size(); ++bus) {
    if (bus > 0)
      layout.inputBuses.set(bus, juce::AudioChannelSet::disabled());
  }
  for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
    layout.outputBuses.set(bus, juce::AudioChannelSet::disabled());

  layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
  layout.outputBuses.set(0, juce::AudioChannelSet::stereo());

  if (!processor.setBusesLayout(layout)) {
    error = "Effect rejected a stereo input/output layout";
    return false;
  }
  return true;
}

void HostedPluginSlot::publishRuntime(std::unique_ptr<Runtime> runtime) {
  runtime_.publish(std::move(runtime));
}

} // namespace fiddle
