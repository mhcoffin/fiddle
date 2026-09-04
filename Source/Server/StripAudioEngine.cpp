#include "StripAudioEngine.h"

#include <utility>

namespace fiddle {
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

StripAudioEngine::StripAudioEngine()
    : gainLinear_(std::make_shared<std::atomic<float>>(1.0f)) {
  graph_.setPlayConfigDetails(2, 2, sampleRate_, blockSize_);
  rebuildGraph();
}

StripAudioEngine::~StripAudioEngine() {
  prepared_.store(false, std::memory_order_release);
  graph_.releaseResources();
  graph_.clear(kNoUpdate);
}

void StripAudioEngine::prepareToPlay(double sampleRate, int blockSize) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
  blockSize_ = juce::jmax(1, blockSize);
  midiScratch_.ensureSize(1024);
  graph_.setPlayConfigDetails(2, 2, sampleRate_, blockSize_);
  graph_.prepareToPlay(sampleRate_, blockSize_);
  prepared_.store(true, std::memory_order_release);
  latencySamples_.store(graph_.getLatencySamples(), std::memory_order_release);
}

void StripAudioEngine::releaseResources() {
  prepared_.store(false, std::memory_order_release);
  graph_.releaseResources();
}

void StripAudioEngine::processBlock(juce::AudioBuffer<float> &audio,
                                    float effectiveGain) {
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

  auto input = graph_.addNode(
      std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode),
      std::nullopt, kNoUpdate);
  auto gain = graph_.addNode(std::make_unique<StripGainProcessor>(gainLinear_),
                             std::nullopt, kNoUpdate);
  auto output = graph_.addNode(
      std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
          juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode),
      std::nullopt, kNoUpdate);

  connectStereo(graph_, input, gain);
  connectStereo(graph_, gain, output);
  graph_.rebuild();
  latencySamples_.store(graph_.getLatencySamples(), std::memory_order_release);
}

} // namespace fiddle
