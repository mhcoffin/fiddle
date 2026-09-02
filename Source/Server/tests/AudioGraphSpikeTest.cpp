#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace allocationProbe {

thread_local bool enabled = false;
thread_local std::size_t count = 0;

void recordAllocation() noexcept {
  if (enabled)
    ++count;
}

} // namespace allocationProbe

void *operator new(std::size_t size) {
  allocationProbe::recordAllocation();
  if (auto *memory = std::malloc(size))
    return memory;
  throw std::bad_alloc();
}

void *operator new[](std::size_t size) {
  allocationProbe::recordAllocation();
  if (auto *memory = std::malloc(size))
    return memory;
  throw std::bad_alloc();
}

void *operator new(std::size_t size, std::align_val_t alignment) {
  allocationProbe::recordAllocation();
  void *memory = nullptr;
  if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size) == 0)
    return memory;
  throw std::bad_alloc();
}

void *operator new[](std::size_t size, std::align_val_t alignment) {
  allocationProbe::recordAllocation();
  void *memory = nullptr;
  if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size) == 0)
    return memory;
  throw std::bad_alloc();
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  std::free(memory);
}
void operator delete(void *memory, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete[](void *memory, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete(void *memory, std::size_t, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete[](void *memory, std::size_t,
                       std::align_val_t) noexcept {
  std::free(memory);
}

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;
constexpr auto kNoUpdate = juce::AudioProcessorGraph::UpdateKind::none;

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                    \
                << "]: " << #condition << std::endl;                          \
    }                                                                          \
  } while (false)

void checkNear(float actual, float expected, float tolerance, int line) {
  if (std::abs(actual - expected) <= tolerance) {
    ++passed;
    return;
  }

  ++failed;
  std::cerr << "FAIL [" << __FILE__ << ':' << line << "]: expected "
            << expected << ", got " << actual << std::endl;
}

#define CHECK_NEAR(actual, expected, tolerance)                                \
  checkNear((actual), (expected), (tolerance), __LINE__)

class TestProcessor : public juce::AudioProcessor {
public:
  enum class BusLayout { none, stereoOutput, stereoEffect };

  TestProcessor(juce::String name, BusLayout busLayout,
                bool acceptsMidi = false, bool producesMidi = false)
      : juce::AudioProcessor(makeBuses(busLayout)), name_(std::move(name)),
        acceptsMidi_(acceptsMidi), producesMidi_(producesMidi) {}

  const juce::String getName() const override { return name_; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override { return acceptsMidi_; }
  bool producesMidi() const override { return producesMidi_; }
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

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override {
    const auto validBus = [](const juce::AudioChannelSet &layout) {
      return layout.isDisabled() || layout == juce::AudioChannelSet::stereo();
    };

    for (const auto &layout : layouts.inputBuses)
      if (!validBus(layout))
        return false;
    for (const auto &layout : layouts.outputBuses)
      if (!validBus(layout))
        return false;
    return true;
  }

private:
  static BusesProperties makeBuses(BusLayout layout) {
    BusesProperties buses;
    if (layout == BusLayout::stereoEffect)
      buses = buses.withInput("Input", juce::AudioChannelSet::stereo(), true);
    if (layout != BusLayout::none)
      buses = buses.withOutput("Output", juce::AudioChannelSet::stereo(), true);
    return buses;
  }

  juce::String name_;
  bool acceptsMidi_ = false;
  bool producesMidi_ = false;
};

class ScheduledMidiSourceProcessor final : public TestProcessor {
public:
  ScheduledMidiSourceProcessor()
      : TestProcessor("Scheduled MIDI Source", BusLayout::none, false, true) {}

  bool schedule(const juce::MidiMessage &message, int sampleOffset) {
    if (eventCount_ == events_.size())
      return false;
    events_[eventCount_++] = {message, sampleOffset};
    return true;
  }

  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &midi) override {
    audio.clear();
    midi.clear();
    for (std::size_t i = 0; i < eventCount_; ++i)
      midi.addEvent(events_[i].message, events_[i].sampleOffset);
    eventCount_ = 0;
  }

private:
  struct ScheduledEvent {
    juce::MidiMessage message;
    int sampleOffset = 0;
  };

  std::array<ScheduledEvent, 8> events_{};
  std::size_t eventCount_ = 0;
};

class ImpulseInstrumentProcessor final : public TestProcessor {
public:
  ImpulseInstrumentProcessor(int expectedNote, float leftAmplitude,
                             float rightAmplitude)
      : TestProcessor("Impulse Instrument", BusLayout::stereoOutput, true,
                      false),
        expectedNote_(expectedNote), leftAmplitude_(leftAmplitude),
        rightAmplitude_(rightAmplitude) {}

  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &midi) override {
    audio.clear();
    for (const auto metadata : midi) {
      const auto message = metadata.getMessage();
      if (!message.isNoteOn())
        continue;

      if (message.getNoteNumber() == expectedNote_)
        ++matchedEvents;
      else
        ++unexpectedEvents;

      const int offset = juce::jlimit(0, audio.getNumSamples() - 1,
                                      metadata.samplePosition);
      if (audio.getNumChannels() > 0)
        audio.addSample(0, offset, leftAmplitude_);
      if (audio.getNumChannels() > 1)
        audio.addSample(1, offset, rightAmplitude_);
    }
  }

  int matchedEvents = 0;
  int unexpectedEvents = 0;

private:
  int expectedNote_ = 0;
  float leftAmplitude_ = 0.0f;
  float rightAmplitude_ = 0.0f;
};

class OneShotImpulseProcessor final : public TestProcessor {
public:
  OneShotImpulseProcessor(float leftAmplitude, float rightAmplitude)
      : TestProcessor("One-shot Impulse", BusLayout::stereoOutput),
        leftAmplitude_(leftAmplitude), rightAmplitude_(rightAmplitude) {}

  void trigger() noexcept { shouldFire_ = true; }

  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &) override {
    audio.clear();
    if (!std::exchange(shouldFire_, false))
      return;
    if (audio.getNumChannels() > 0)
      audio.setSample(0, 0, leftAmplitude_);
    if (audio.getNumChannels() > 1)
      audio.setSample(1, 0, rightAmplitude_);
  }

private:
  float leftAmplitude_ = 0.0f;
  float rightAmplitude_ = 0.0f;
  bool shouldFire_ = true;
};

class ConstantProcessor final : public TestProcessor {
public:
  explicit ConstantProcessor(float value)
      : TestProcessor("Constant", BusLayout::stereoOutput), value_(value) {}

  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &) override {
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
      juce::FloatVectorOperations::fill(audio.getWritePointer(channel), value_,
                                        audio.getNumSamples());
  }

private:
  float value_ = 0.0f;
};

class GainProcessor final : public TestProcessor {
public:
  explicit GainProcessor(float gain)
      : TestProcessor("Gain", BusLayout::stereoEffect), gain_(gain) {}

  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &) override {
    audio.applyGain(gain_);
  }

private:
  float gain_ = 1.0f;
};

class FixedLatencyProcessor final : public TestProcessor {
public:
  explicit FixedLatencyProcessor(int latencySamples)
      : TestProcessor("Fixed Latency", BusLayout::stereoEffect) {
    setDelaySamples(latencySamples);
  }

  void setDelaySamples(int latencySamples) {
    latencySamples_ = juce::jmax(0, latencySamples);
    setLatencySamples(latencySamples_);
    for (auto &channel : delayLines_)
      channel.assign(static_cast<std::size_t>(juce::jmax(1, latencySamples_)),
                     0.0f);
    writeIndex_ = 0;
  }

  void prepareToPlay(double, int) override { reset(); }

  void reset() override {
    for (auto &channel : delayLines_)
      std::fill(channel.begin(), channel.end(), 0.0f);
    writeIndex_ = 0;
  }

  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &) override {
    if (latencySamples_ == 0)
      return;

    const int channels = juce::jmin(audio.getNumChannels(),
                                    static_cast<int>(delayLines_.size()));
    for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
      for (int channel = 0; channel < channels; ++channel) {
        auto &delayLine = delayLines_[static_cast<std::size_t>(channel)];
        const float input = audio.getSample(channel, sample);
        audio.setSample(channel, sample, delayLine[writeIndex_]);
        delayLine[writeIndex_] = input;
      }
      writeIndex_ = (writeIndex_ + 1) % delayLines_[0].size();
    }
  }

private:
  std::array<std::vector<float>, 2> delayLines_;
  std::size_t writeIndex_ = 0;
  int latencySamples_ = 0;
};

using Graph = juce::AudioProcessorGraph;
using Node = Graph::Node;

Node::Ptr addAudioOutput(Graph &graph) {
  return graph.addNode(
      std::make_unique<Graph::AudioGraphIOProcessor>(
          Graph::AudioGraphIOProcessor::audioOutputNode),
      std::nullopt, kNoUpdate);
}

bool connectStereo(Graph &graph, Node::Ptr source, Node::Ptr destination) {
  bool connected = source != nullptr && destination != nullptr;
  for (int channel = 0; connected && channel < 2; ++channel) {
    connected &= graph.addConnection(
        {{source->nodeID, channel}, {destination->nodeID, channel}},
        kNoUpdate);
  }
  return connected;
}

bool disconnectStereo(Graph &graph, Node::Ptr source, Node::Ptr destination) {
  bool disconnected = source != nullptr && destination != nullptr;
  for (int channel = 0; disconnected && channel < 2; ++channel) {
    disconnected &= graph.removeConnection(
        {{source->nodeID, channel}, {destination->nodeID, channel}},
        kNoUpdate);
  }
  return disconnected;
}

void prepareStereoOutputGraph(Graph &graph) {
  graph.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
}

void testScheduledMidiIsolation() {
  Graph graph;
  prepareStereoOutputGraph(graph);

  auto sourceAOwner = std::make_unique<ScheduledMidiSourceProcessor>();
  auto *sourceAProcessor = sourceAOwner.get();
  const auto sourceA =
      graph.addNode(std::move(sourceAOwner), std::nullopt, kNoUpdate);

  auto sourceBOwner = std::make_unique<ScheduledMidiSourceProcessor>();
  auto *sourceBProcessor = sourceBOwner.get();
  const auto sourceB =
      graph.addNode(std::move(sourceBOwner), std::nullopt, kNoUpdate);

  auto instrumentAOwner =
      std::make_unique<ImpulseInstrumentProcessor>(60, 0.25f, 0.125f);
  auto *instrumentAProcessor = instrumentAOwner.get();
  const auto instrumentA =
      graph.addNode(std::move(instrumentAOwner), std::nullopt, kNoUpdate);

  auto instrumentBOwner =
      std::make_unique<ImpulseInstrumentProcessor>(67, 0.75f, 0.375f);
  auto *instrumentBProcessor = instrumentBOwner.get();
  const auto instrumentB =
      graph.addNode(std::move(instrumentBOwner), std::nullopt, kNoUpdate);

  const auto output = addAudioOutput(graph);
  CHECK(graph.addConnection(
      {{sourceA->nodeID, Graph::midiChannelIndex},
       {instrumentA->nodeID, Graph::midiChannelIndex}},
      kNoUpdate));
  CHECK(graph.addConnection(
      {{sourceB->nodeID, Graph::midiChannelIndex},
       {instrumentB->nodeID, Graph::midiChannelIndex}},
      kNoUpdate));
  CHECK(connectStereo(graph, instrumentA, output));
  CHECK(connectStereo(graph, instrumentB, output));

  graph.prepareToPlay(kSampleRate, kBlockSize);

  CHECK(sourceAProcessor->schedule(juce::MidiMessage::noteOn(1, 60, 1.0f), 7));
  CHECK(sourceBProcessor->schedule(juce::MidiMessage::noteOn(1, 67, 1.0f),
                                   19));

  juce::AudioBuffer<float> audio(2, kBlockSize);
  juce::MidiBuffer midi;
  midi.ensureSize(4096);
  audio.clear();
  graph.processBlock(audio, midi);

  CHECK(instrumentAProcessor->matchedEvents == 1);
  CHECK(instrumentAProcessor->unexpectedEvents == 0);
  CHECK(instrumentBProcessor->matchedEvents == 1);
  CHECK(instrumentBProcessor->unexpectedEvents == 0);
  CHECK_NEAR(audio.getSample(0, 7), 0.25f, 1.0e-6f);
  CHECK_NEAR(audio.getSample(1, 7), 0.125f, 1.0e-6f);
  CHECK_NEAR(audio.getSample(0, 19), 0.75f, 1.0e-6f);
  CHECK_NEAR(audio.getSample(1, 19), 0.375f, 1.0e-6f);
  CHECK_NEAR(audio.getMagnitude(0, 0, 7), 0.0f, 1.0e-6f);
}

void testDrySendLatencyCompensationAndRebuild() {
  Graph graph;
  prepareStereoOutputGraph(graph);

  auto sourceOwner = std::make_unique<OneShotImpulseProcessor>(1.0f, 0.5f);
  auto *sourceProcessor = sourceOwner.get();
  const auto source =
      graph.addNode(std::move(sourceOwner), std::nullopt, kNoUpdate);
  const auto insert = graph.addNode(std::make_unique<GainProcessor>(2.0f),
                                    std::nullopt, kNoUpdate);
  const auto send = graph.addNode(std::make_unique<GainProcessor>(0.25f),
                                  std::nullopt, kNoUpdate);

  auto latencyOwner = std::make_unique<FixedLatencyProcessor>(8);
  auto *latencyProcessor = latencyOwner.get();
  const auto auxReturn =
      graph.addNode(std::move(latencyOwner), std::nullopt, kNoUpdate);
  const auto master = graph.addNode(std::make_unique<GainProcessor>(0.5f),
                                    std::nullopt, kNoUpdate);
  const auto output = addAudioOutput(graph);

  CHECK(connectStereo(graph, source, insert));
  CHECK(connectStereo(graph, insert, master));
  CHECK(connectStereo(graph, insert, send));
  CHECK(connectStereo(graph, send, auxReturn));
  CHECK(connectStereo(graph, auxReturn, master));
  CHECK(connectStereo(graph, master, output));

  graph.prepareToPlay(kSampleRate, kBlockSize);
  CHECK(graph.getLatencySamples() == 8);

  juce::AudioBuffer<float> audio(2, kBlockSize);
  juce::MidiBuffer midi;
  midi.ensureSize(4096);
  audio.clear();
  graph.processBlock(audio, midi);

  CHECK_NEAR(audio.getSample(0, 8), 1.25f, 1.0e-6f);
  CHECK_NEAR(audio.getSample(1, 8), 0.625f, 1.0e-6f);
  CHECK_NEAR(audio.getMagnitude(0, 0, 8), 0.0f, 1.0e-6f);
  CHECK_NEAR(audio.getMagnitude(0, 9, kBlockSize - 9), 0.0f, 1.0e-6f);

  latencyProcessor->setDelaySamples(12);
  sourceProcessor->trigger();
  graph.rebuild();
  CHECK(graph.getLatencySamples() == 12);

  audio.clear();
  midi.clear();
  graph.processBlock(audio, midi);

  CHECK_NEAR(audio.getSample(0, 12), 1.25f, 1.0e-6f);
  CHECK_NEAR(audio.getSample(1, 12), 0.625f, 1.0e-6f);
  CHECK_NEAR(audio.getMagnitude(0, 0, 12), 0.0f, 1.0e-6f);
}

struct SchedulingAdjustment {
  double graphLatencyMs = 0.0;
  double triggerDelayMs = 0.0;
  double minimumSafePlaybackDelayMs = 0.0;
  bool latencyExceedsBudget = false;
};

SchedulingAdjustment calculateSchedulingAdjustment(int playbackDelayMs,
                                                   int graphLatencySamples,
                                                   double sampleRate,
                                                   double transportHeadroomMs) {
  const double graphLatencyMs =
      sampleRate > 0.0
          ? 1000.0 * static_cast<double>(juce::jmax(0, graphLatencySamples)) /
                sampleRate
          : 0.0;
  const double available = static_cast<double>(playbackDelayMs) -
                           transportHeadroomMs - graphLatencyMs;
  return {graphLatencyMs, juce::jmax(0.0, available),
          transportHeadroomMs + graphLatencyMs, available < 0.0};
}

void testSchedulingAdjustment() {
  const auto normal = calculateSchedulingAdjustment(1000, 480, 48000.0, 40.0);
  CHECK_NEAR(static_cast<float>(normal.graphLatencyMs), 10.0f, 1.0e-6f);
  CHECK_NEAR(static_cast<float>(normal.triggerDelayMs), 950.0f, 1.0e-6f);
  CHECK_NEAR(static_cast<float>(normal.minimumSafePlaybackDelayMs), 50.0f,
             1.0e-6f);
  CHECK(!normal.latencyExceedsBudget);

  const auto insufficient =
      calculateSchedulingAdjustment(45, 480, 48000.0, 40.0);
  CHECK_NEAR(static_cast<float>(insufficient.triggerDelayMs), 0.0f, 1.0e-6f);
  CHECK(insufficient.latencyExceedsBudget);
}

void testRealtimeTopologyExchange() {
  Graph graph;
  prepareStereoOutputGraph(graph);

  const auto source = graph.addNode(std::make_unique<ConstantProcessor>(1.0f),
                                    std::nullopt, kNoUpdate);
  const auto output = addAudioOutput(graph);
  CHECK(connectStereo(graph, source, output));
  graph.prepareToPlay(kSampleRate, kBlockSize);

  std::atomic<bool> stop{false};
  std::atomic<int> blocksProcessed{0};
  std::atomic<int> invalidSamples{0};
  std::atomic<int> directBlocks{0};
  std::atomic<int> insertedBlocks{0};

  std::thread audioThread([&] {
    juce::AudioBuffer<float> audio(2, kBlockSize);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);

    while (!stop.load(std::memory_order_acquire)) {
      audio.clear();
      midi.clear();
      graph.processBlock(audio, midi);

      const float first = audio.getSample(0, 0);
      if (std::abs(first - 1.0f) <= 1.0e-6f)
        directBlocks.fetch_add(1, std::memory_order_relaxed);
      else if (std::abs(first - 0.25f) <= 1.0e-6f)
        insertedBlocks.fetch_add(1, std::memory_order_relaxed);
      else
        invalidSamples.fetch_add(1, std::memory_order_relaxed);

      for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
        for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
          const float value = audio.getSample(channel, sample);
          if (std::abs(value - first) > 1.0e-6f)
            invalidSamples.fetch_add(1, std::memory_order_relaxed);
        }
      }
      blocksProcessed.fetch_add(1, std::memory_order_release);
    }
  });

  while (blocksProcessed.load(std::memory_order_acquire) < 8)
    std::this_thread::yield();

  for (int iteration = 0; iteration < 64; ++iteration) {
    CHECK(disconnectStereo(graph, source, output));
    const auto inserted = graph.addNode(
        std::make_unique<GainProcessor>(0.25f), std::nullopt, kNoUpdate);
    CHECK(connectStereo(graph, source, inserted));
    CHECK(connectStereo(graph, inserted, output));
    graph.rebuild();
    std::this_thread::yield();

    CHECK(disconnectStereo(graph, source, inserted));
    CHECK(disconnectStereo(graph, inserted, output));
    CHECK(connectStereo(graph, source, output));
    const auto removed = graph.removeNode(inserted->nodeID, kNoUpdate);
    CHECK(removed != nullptr);
    graph.rebuild();
    std::this_thread::yield();
  }

  while (blocksProcessed.load(std::memory_order_acquire) < 128)
    std::this_thread::yield();
  stop.store(true, std::memory_order_release);
  audioThread.join();

  CHECK(invalidSamples.load(std::memory_order_relaxed) == 0);
  CHECK(directBlocks.load(std::memory_order_relaxed) > 0);
  CHECK(insertedBlocks.load(std::memory_order_relaxed) > 0);
}

void testNoSteadyStateHeapAllocation() {
  Graph graph;
  prepareStereoOutputGraph(graph);

  auto sourceOwner = std::make_unique<ScheduledMidiSourceProcessor>();
  auto *sourceProcessor = sourceOwner.get();
  const auto source =
      graph.addNode(std::move(sourceOwner), std::nullopt, kNoUpdate);
  const auto instrument = graph.addNode(
      std::make_unique<ImpulseInstrumentProcessor>(60, 1.0f, 0.5f),
      std::nullopt, kNoUpdate);
  const auto insert = graph.addNode(std::make_unique<GainProcessor>(0.5f),
                                    std::nullopt, kNoUpdate);
  const auto send = graph.addNode(std::make_unique<GainProcessor>(0.25f),
                                  std::nullopt, kNoUpdate);
  const auto auxReturn = graph.addNode(
      std::make_unique<FixedLatencyProcessor>(8), std::nullopt, kNoUpdate);
  const auto master = graph.addNode(std::make_unique<GainProcessor>(0.5f),
                                    std::nullopt, kNoUpdate);
  const auto output = addAudioOutput(graph);
  CHECK(graph.addConnection(
      {{source->nodeID, Graph::midiChannelIndex},
       {instrument->nodeID, Graph::midiChannelIndex}},
      kNoUpdate));
  CHECK(connectStereo(graph, instrument, insert));
  CHECK(connectStereo(graph, insert, master));
  CHECK(connectStereo(graph, insert, send));
  CHECK(connectStereo(graph, send, auxReturn));
  CHECK(connectStereo(graph, auxReturn, master));
  CHECK(connectStereo(graph, master, output));
  graph.prepareToPlay(kSampleRate, kBlockSize);

  juce::AudioBuffer<float> audio(2, kBlockSize);
  juce::MidiBuffer midi;
  midi.ensureSize(4096);
  for (int block = 0; block < 8; ++block) {
    CHECK(sourceProcessor->schedule(
        juce::MidiMessage::noteOn(1, 60, 1.0f), 0));
    audio.clear();
    midi.clear();
    graph.processBlock(audio, midi);
  }

  allocationProbe::count = 0;
  allocationProbe::enabled = true;
  for (int block = 0; block < 256; ++block) {
    sourceProcessor->schedule(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    audio.clear();
    midi.clear();
    graph.processBlock(audio, midi);
  }
  allocationProbe::enabled = false;

  CHECK(allocationProbe::count == 0);
  CHECK_NEAR(audio.getSample(0, 8), 0.3125f, 1.0e-6f);
  CHECK_NEAR(audio.getSample(1, 8), 0.15625f, 1.0e-6f);
}

} // namespace

int main() {
  const juce::ScopedJuceInitialiser_GUI juceInitialiser;
  testScheduledMidiIsolation();
  testDrySendLatencyCompensationAndRebuild();
  testSchedulingAdjustment();
  testRealtimeTopologyExchange();
  testNoSteadyStateHeapAllocation();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
