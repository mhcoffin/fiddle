#include "FiddleDatabase.h"
#include "GroupBusCommandService.h"
#include "GroupBusJsHandlers.h"
#include "MessageRouter.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "ProjectRestoreService.h"
#include "StateManager.h"
#include "UndoManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *expression, int line) {
  if (!condition)
    throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
}
#define REQUIRE(condition) require(bool(condition), #condition, __LINE__)

// An exact, MIDI-gated constant instrument or a gain/delay effect. The delay
// really delays samples, so latency tests cannot pass on metadata alone.
juce::PluginDescription description(bool instrument);

class SignalProcessor final : public juce::AudioPluginInstance {
public:
  inline static int programQueries = 0;
  inline static int stateCaptures = 0;
  inline static std::vector<std::pair<double, int>> preparations;
  SignalProcessor(bool instrument, float amount, int latency = 0)
      : AudioPluginInstance(instrument
                           ? BusesProperties().withOutput(
                                 "Output", juce::AudioChannelSet::stereo(), true)
                           : BusesProperties()
                                 .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                 .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        instrument_(instrument), amount_(amount), latency_(latency) {
    setLatencySamples(latency);
  }
  const juce::String getName() const override { return "Deterministic signal"; }
  void fillInPluginDescription(juce::PluginDescription &result) const override {
    result = description(instrument_);
  }
  void prepareToPlay(double rate, int block) override {
    preparations.emplace_back(rate, block);
    delay_.setSize(2, std::max(1, latency_));
    delay_.clear();
    position_ = 0;
    notes_.fill(false);
  }
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &audio, juce::MidiBuffer &midi) override {
    auto event = midi.cbegin();
    for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
      while (event != midi.cend() && (*event).samplePosition <= sample) {
        const auto message = (*event).getMessage();
        if (message.isNoteOn())
          notes_[message.getNoteNumber()] = true;
        else if (message.isNoteOff())
          notes_[message.getNoteNumber()] = false;
        else if (message.isAllNotesOff() || message.isAllSoundOff())
          notes_.fill(false);
        ++event;
      }
      const bool playing = std::any_of(notes_.begin(), notes_.end(),
                                       [](bool value) { return value; });
      for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
        float value = instrument_ ? (playing ? amount_ : 0.0f)
                                  : audio.getSample(channel, sample) * amount_;
        if (latency_ > 0) {
          const float delayed = delay_.getSample(channel, position_);
          delay_.setSample(channel, position_, value);
          value = delayed;
        }
        audio.setSample(channel, sample, value);
      }
      if (latency_ > 0)
        position_ = (position_ + 1) % latency_;
    }
  }
  double getTailLengthSeconds() const override { return 0; }
  bool acceptsMidi() const override { return instrument_; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  int getNumPrograms() override { ++programQueries; return 1; }
  int getCurrentProgram() override { ++programQueries; return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { ++programQueries; return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &state) override {
    ++stateCaptures;
    state.replaceAll(&amount_, sizeof(amount_));
  }
  void setStateInformation(const void *data, int size) override {
    if (size == sizeof(amount_))
      std::memcpy(&amount_, data, sizeof(amount_));
  }
private:
  bool instrument_;
  float amount_;
  int latency_, position_ = 0;
  std::array<bool, 128> notes_{};
  juce::AudioBuffer<float> delay_;
};

juce::PluginDescription description(bool instrument) {
  juce::PluginDescription result;
  result.name = instrument ? "Test instrument" : "Test effect";
  result.pluginFormatName = "Test";
  result.uniqueId = instrument ? 101 : 102;
  result.isInstrument = instrument;
  result.numInputChannels = instrument ? 0 : 2;
  result.numOutputChannels = 2;
  return result;
}

// No vendor binaries or installed catalog required. Goes through JUCE's
// asynchronous format manager and the production HostedPluginSlot loader.
class TestPluginFormat final : public juce::AudioPluginFormat {
public:
  bool missingInstruments = false;
  bool missingEffects = false;
  bool defer = false;
  std::vector<std::function<void()>> pending;
  juce::String getName() const override { return "Test"; }
  void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription> &, const juce::String &) override {}
  bool fileMightContainThisPluginType(const juce::String &) override { return true; }
  juce::String getNameOfPluginFromIdentifier(const juce::String &id) override { return id; }
  bool pluginNeedsRescanning(const juce::PluginDescription &) override { return false; }
  bool doesPluginStillExist(const juce::PluginDescription &) override { return true; }
  bool canScanForPlugins() const override { return false; }
  bool isTrivialToScan() const override { return true; }
  juce::StringArray searchPathsForPlugins(const juce::FileSearchPath &, bool, bool) override { return {}; }
  juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
  bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription &) const override { return false; }
  void completeAll() {
    auto callbacks = std::move(pending);
    pending.clear();
    for (auto &callback : callbacks)
      callback();
  }
private:
  void createPluginInstance(const juce::PluginDescription &desc, double, int,
                            PluginCreationCallback callback) override {
    const bool missing = desc.isInstrument ? missingInstruments : missingEffects;
    auto create = [desc, missing, callback = std::move(callback)] {
      // Deliberately unlike any saved amount: ignoring setStateInformation
      // makes the rendered-audio comparisons fail.
      if (missing)
        callback(nullptr, "Intentionally unavailable test plug-in");
      else
        callback(std::make_unique<SignalProcessor>(desc.isInstrument, 0.9375f), {});
    };
    if (defer)
      pending.push_back(std::move(create));
    else
      create();
  }
};

void pumpUntil(const std::function<bool()> &done) {
  const auto deadline = juce::Time::getMillisecondCounter() + 5000;
  while (!done() && juce::Time::getMillisecondCounter() < deadline)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  REQUIRE(done());
}

void addEffect(fiddle::StripAudioEngine &engine, const juce::String &id,
               float amount, int latency = 0,
               fiddle::StripInsertPosition position = fiddle::StripInsertPosition::preFader) {
  fiddle::AudioInsertSnapshot slot;
  slot.slotId = id;
  slot.description = description(false);
  REQUIRE(engine.insertProcessor(slot, position, engine.insertCount(position),
                                  std::make_unique<SignalProcessor>(false, amount, latency)));
}

struct MixerFixture {
  static constexpr int blockSize = 64;
  static constexpr double sampleRate = 48000;
  fiddle::MixerModel mixer;
  fiddle::PluginScanner scanner; // Constructor does not scan or load vendor plug-ins.
  fiddle::UndoManager undo;
  fiddle::GroupBusCommandService commands{mixer, scanner, undo};
  fiddle::MessageRouter router;
  int changes = 0;
  fiddle::GroupBusJsHandlers handlers{
      router, commands, {{}, [this] { ++changes; }, {}}};
  double now = juce::Time::getMillisecondCounterHiRes();

  MixerFixture() {
    mixer.prepareToPlay(sampleRate, blockSize);
    handlers.registerHandlers();
  }
  juce::String instrument(float amount, int channel) {
    const auto id = mixer.addStrip();
    auto *strip = mixer.getStrip(id);
    strip->setInputAssignment(0, channel);
    juce::String error;
    REQUIRE(strip->installInstrumentProcessor(
        description(true), std::make_unique<SignalProcessor>(true, amount), error));
    return id;
  }
  void noteOn(int channel, double offsetMs = 0) {
    mixer.routeNoteEvent(0, channel, juce::MidiMessage::noteOn(1, 60, 1.0f),
                         now + offsetMs);
  }
  juce::AudioBuffer<float> render() {
    juce::AudioBuffer<float> audio(2, blockSize);
    audio.clear();
    mixer.processBlock(audio, now);
    now += 1000.0 * blockSize / sampleRate;
    return audio;
  }
  void expectLevel(float expected) {
    const auto audio = render();
    for (int channel = 0; channel < 2; ++channel)
      for (int sample = 0; sample < blockSize; ++sample)
        REQUIRE(std::abs(audio.getSample(channel, sample) - expected) < 0.00001f);
  }
};

void testMeterOnlySnapshots() {
  MixerFixture f;
  const auto a = f.instrument(0.1f, 1);
  const auto b = f.instrument(0.2f, 2);
  REQUIRE(f.commands.addGroupBus("Strings", {a}));
  const auto busId = f.mixer.getAllGroupBuses().front()->id;
  f.noteOn(1);
  f.noteOn(2);
  f.expectLevel(0.3f);
  const auto programs = SignalProcessor::programQueries;
  const auto captures = SignalProcessor::stateCaptures;
  const auto snapshot = f.mixer.meterLevels();
  REQUIRE(snapshot.getDynamicObject()->getProperties().size() == 3);
  REQUIRE(snapshot["strips"].getDynamicObject()->getProperties().size() == 2);
  REQUIRE(snapshot["buses"].getDynamicObject()->getProperties().size() == 1);
  REQUIRE(static_cast<double>(snapshot["strips"][juce::Identifier(a)][0]) == f.mixer.getStrip(a)->peakDb());
  REQUIRE(static_cast<double>(snapshot["strips"][juce::Identifier(a)][1]) == f.mixer.getStrip(a)->peakHoldDb());
  REQUIRE(static_cast<double>(snapshot["buses"][juce::Identifier(busId)][0]) == f.mixer.getGroupBus(busId)->peakDb());
  REQUIRE(static_cast<double>(snapshot["buses"][juce::Identifier(busId)][1]) == f.mixer.getGroupBus(busId)->peakHoldDb());
  REQUIRE(static_cast<double>(snapshot["masterPeakDb"]) == f.mixer.masterAudio().peakDb());
  for (int i = 0; i < 100; ++i) {
    const auto json = juce::JSON::toString(f.mixer.meterLevels(), true);
    REQUIRE(!json.contains("pluginUid") && !json.contains("gainDb"));
  }
  REQUIRE(SignalProcessor::programQueries == programs);
  REQUIRE(SignalProcessor::stateCaptures == captures);
  f.mixer.removeStrip(b);
  REQUIRE(!f.mixer.meterLevels()["strips"].hasProperty(b));
  // Demonstrate the test detects the old full-state path's program queries.
  const auto full = f.mixer.toJson();
  REQUIRE(full.isNotEmpty() && SignalProcessor::programQueries > programs);
}

void testPluginTimingAndReprepare() {
  MixerFixture f;
  const auto a = f.instrument(0.1f, 1);
  const auto b = f.instrument(0.2f, 2);
  REQUIRE(f.commands.addGroupBus("Strings", {a}));
  auto *bus = f.mixer.getAllGroupBuses().front();
  addEffect(f.mixer.getStrip(a)->audioEngine(), "strip-timed", 2.0f);
  addEffect(bus->audioEngine(), "bus-timed", 0.5f);
  fiddle::MasterInsertSnapshot master;
  master.slotId = "master-timed";
  master.description = description(false);
  REQUIRE(f.mixer.masterAudio().insertProcessor(master, 0, std::make_unique<SignalProcessor>(false, 1.0f)));
  f.noteOn(1); f.noteOn(2);
  for (int i = 0; i < 200; ++i) {
    fiddle::PluginRenderDiagnostics::beginBlock();
    f.expectLevel(0.3f);
    REQUIRE(fiddle::PluginRenderDiagnostics::blockWorkMs() >= 0);
  }
  const auto programs = SignalProcessor::programQueries;
  const auto captures = SignalProcessor::stateCaptures;
  auto rows = f.mixer.pluginTimings(juce::Time::getMillisecondCounterHiRes());
  REQUIRE(rows.isArray() && rows.size() == 5); // two instruments + strip/bus/master FX
  for (const auto &row : *rows.getArray()) {
    REQUIRE(static_cast<juce::int64>(row["calls"]) > 0);
    REQUIRE(static_cast<int>(row["blockSize"]) == 64);
    REQUIRE(static_cast<double>(row["sampleRate"]) == 48000);
    REQUIRE(static_cast<double>(row["maxMs"]) >= static_cast<double>(row["averageMs"]));
  }
  for (int i = 0; i < 100; ++i)
    REQUIRE(juce::JSON::toString(f.mixer.pluginTimings(f.now)).isNotEmpty());
  REQUIRE(SignalProcessor::programQueries == programs && SignalProcessor::stateCaptures == captures);

  SignalProcessor::preparations.clear();
  f.mixer.prepareToPlay(96000, 1024);
  REQUIRE(SignalProcessor::preparations.size() == 5);
  for (const auto &prepared : SignalProcessor::preparations)
    REQUIRE(prepared.first == 96000 && prepared.second == 1024);
  f.noteOn(1); f.noteOn(2);
  for (int i = 0; i < 30; ++i) {
    juce::AudioBuffer<float> audio(2, 1024); audio.clear();
    f.mixer.processBlock(audio, f.now);
    f.now += 1000.0 * 1024 / 96000;
    REQUIRE(std::abs(audio.getSample(0, 1023) - 0.3f) < 0.00001f);
  }
  rows = f.mixer.pluginTimings(juce::Time::getMillisecondCounterHiRes());
  for (const auto &row : *rows.getArray()) {
    REQUIRE(static_cast<int>(row["blockSize"]) == 1024);
    REQUIRE(static_cast<double>(row["sampleRate"]) == 96000);
  }
  REQUIRE(f.mixer.getStrip(a)->audioEngine().setBypassed("strip-timed", true));
  rows = f.mixer.pluginTimings(f.now);
  bool bypassFound = false;
  for (const auto &row : *rows.getArray())
    if (row["id"].toString() == "strip-timed") bypassFound = static_cast<bool>(row["bypassed"]);
  REQUIRE(bypassFound);
  f.mixer.removeStrip(b);
  REQUIRE(f.mixer.pluginTimings(f.now).size() == 4);
}

void testProductionRoutingAndAudibility() {
  MixerFixture f;
  const auto a = f.instrument(0.1f, 1);
  const auto b = f.instrument(0.2f, 2);
  const auto c = f.instrument(0.4f, 3);
  REQUIRE(f.commands.addGroupBus("Strings", {a, b}));
  const auto bus = f.mixer.getAllGroupBuses().front()->id;
  addEffect(f.mixer.getGroupBus(bus)->audioEngine(), "double", 2.0f);
  for (int channel = 1; channel <= 3; ++channel)
    f.noteOn(channel);
  f.expectLevel(1.0f); // (0.1 + 0.2) * 2 + 0.4: no duplicate direct path.
  REQUIRE(!f.commands.setStripDirectOutput(a, "nonexistent"));
  REQUIRE(f.mixer.getStrip(a)->directOutputBusId == bus);
  REQUIRE(f.commands.setGroupBusMute(bus, true));
  f.expectLevel(0.4f);
  REQUIRE(f.commands.setGroupBusMute(bus, false));
  REQUIRE(f.commands.setGroupBusSolo(bus, true));
  f.expectLevel(0.6f);
  REQUIRE(f.commands.setGroupBusSolo(bus, false));
  f.mixer.getStrip(a)->setSoloed(true);
  f.expectLevel(0.2f);
  f.mixer.getStrip(a)->setSoloed(false);
  f.mixer.getStrip(b)->setActive(false);
  f.expectLevel(0.6f);
  f.mixer.getStrip(b)->setActive(true);
  f.mixer.getStrip(a)->setMuted(true);
  f.expectLevel(0.8f);
  f.mixer.getStrip(a)->setMuted(false);
  f.mixer.masterAudio().setGainDb(juce::Decibels::gainToDecibels(0.5f));
  f.expectLevel(0.5f);
  REQUIRE(f.mixer.getStrip(c)->directOutputBusId.isEmpty());
}

void testBusRemovalThroughUiCommandsAndUndo() {
  MixerFixture f;
  const auto a = f.instrument(0.1f, 1);
  const auto b = f.instrument(0.2f, 2);
  REQUIRE(f.commands.addGroupBus("First", {}));
  REQUIRE(f.commands.addGroupBus("Strings", {a, b}));
  const auto bus = f.mixer.getAllGroupBuses().back()->id;
  addEffect(f.mixer.getGroupBus(bus)->audioEngine(), "double", 2.0f);
  f.noteOn(1);
  f.noteOn(2);
  f.expectLevel(0.6f);
  // Exercise the same message -> handler -> service -> undo action used by UI.
  for (int iteration = 0; iteration < 20; ++iteration) {
    juce::Array<juce::var> args{bus};
    REQUIRE(f.router.handleMessage("removeGroupBus", juce::var(args)));
    REQUIRE(f.changes == iteration + 1);
    REQUIRE(f.mixer.getGroupBus(bus) == nullptr);
    REQUIRE(f.mixer.getStrip(a)->directOutputBusId.isEmpty());
    REQUIRE(f.mixer.getStrip(b)->directOutputBusId.isEmpty());
    f.expectLevel(0.3f);
    REQUIRE(f.undo.undo());
    REQUIRE(f.mixer.groupBusIndex(bus) == 1);
    REQUIRE(f.mixer.getStrip(a)->directOutputBusId == bus);
    REQUIRE(f.mixer.getStrip(b)->directOutputBusId == bus);
    REQUIRE(f.mixer.getGroupBus(bus)->audioEngine().snapshot("double").has_value());
    f.expectLevel(0.6f);
    REQUIRE(f.undo.redo());
    f.expectLevel(0.3f);
    REQUIRE(f.undo.undo());
    f.expectLevel(0.6f);
  }
}

void testRealSampleLatencyAcrossDirectAndBusRoutes() {
  MixerFixture f;
  f.instrument(0.1f, 1);
  const auto b = f.instrument(0.2f, 1);
  REQUIRE(f.commands.addGroupBus("Delayed", {b}));
  const auto bus = f.mixer.getAllGroupBuses().front()->id;
  addEffect(f.mixer.getStrip(b)->audioEngine(), "strip-delay", 1.0f, 4);
  addEffect(f.mixer.getGroupBus(bus)->audioEngine(), "bus-delay", 1.0f, 8);
  f.mixer.refreshAudioRouting();
  f.noteOn(1, 1.0); // Sample 48, after compensation on the delayed route.
  const auto audio = f.render();
  for (int channel = 0; channel < 2; ++channel)
    for (int sample = 0; sample < MixerFixture::blockSize; ++sample)
      REQUIRE(std::abs(audio.getSample(channel, sample) -
                       (sample < 48 ? 0.0f : 0.3f)) < 0.00001f);
}

void testPanicClearsSoundingAndFutureNotes() {
  MixerFixture f;
  const auto a = f.instrument(0.1f, 1);
  REQUIRE(f.commands.addGroupBus("Strings", {a}));
  for (int iteration = 0; iteration < 40; ++iteration) {
    f.noteOn(1);
    f.expectLevel(0.1f);
    f.noteOn(1, 5.0);
    f.mixer.allNotesOff();
    for (int block = 0; block < 8; ++block)
      f.expectLevel(0.0f);
  }
}

void testGracefulStopWithIncompleteNoteTracking() {
  MixerFixture f;
  const auto a = f.instrument(0.1f, 1);
  REQUIRE(f.commands.addGroupBus("Strings", {a}));
  for (int iteration = 0; iteration < 40; ++iteration) {
    f.noteOn(1);
    f.expectLevel(0.1f);
    f.noteOn(1, 500.0); // A delayed note must not sound after stopping.
    std::vector<fiddle::Note> tracked;
    if (iteration % 2 == 0) {
      fiddle::Note note;
      note.set_port(0);
      note.set_channel(2); // Note protobuf is 1-based; strip input is 0-based.
      note.set_note_number(60);
      tracked.push_back(note);
    }
    f.mixer.gracefulStop(tracked, 20.0);
    // Advance simulated audio time past the release, hard-kill, and queued
    // note deadlines. No wall-clock sleeping or human listening is needed.
    f.now = std::max(f.now, juce::Time::getMillisecondCounterHiRes()) + 1000.0;
    for (int block = 0; block < 8; ++block)
      f.expectLevel(0.0f);
  }
}

struct Sandbox {
  juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("fiddle-integration-" + juce::Uuid().toString());
  Sandbox() { REQUIRE(directory.createDirectory().wasOk()); }
  ~Sandbox() { directory.deleteRecursively(); }
};

void testSnapshotSurvivesDatabaseReopenAndStateExchange() {
  Sandbox sandbox;
  const auto databaseFile = sandbox.directory.getChildFile("test.sqlite");
  const auto stateFile = sandbox.directory.getChildFile("host-a/state.bin");
  const auto otherStateFile = sandbox.directory.getChildFile("host-b/state.bin");
  fiddle::versioning::VersionId savedVersion;
  fiddle::versioning::Hash savedHash;
  std::string branch;
  std::string busId;
  juce::MemoryBlock savedBlob;
  {
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(databaseFile));
    fiddle::versioning::VersionStore versions(*db.getVersionStorage());
    const auto root = versions.initializeEmpty();
    branch = versions.getVersion(root)->branchId;
    MixerFixture f;
    const auto a = f.instrument(0.1f, 1);
    const auto b = f.instrument(0.2f, 2);
    REQUIRE(f.commands.addGroupBus("Saved Strings", {a, b}));
    auto *bus = f.mixer.getAllGroupBuses().front();
    busId = bus->id.toStdString();
    bus->setGainDb(-3.0f);
    bus->setMuted(true);
    bus->setSoloed(true);
    addEffect(bus->audioEngine(), "pre", 2.0f);
    addEffect(bus->audioEngine(), "post", 0.25f, 0,
               fiddle::StripInsertPosition::postFader);
    REQUIRE(bus->audioEngine().setBypassed("post", true));
    f.mixer.getStrip(a)->setMuted(true);
    f.mixer.getStrip(b)->setGainDb(-9.0f);
    f.mixer.masterAudio().setGainDb(-6.0f);

    fiddle::StateManager state;
    state.setVersionStore(&versions);
    state.setCurrentBranchId(branch);
    state.setConfigName("Integration");
    state.initialize(stateFile);
    savedVersion = state.commitCurrentState(f.mixer, branch);
    REQUIRE(!savedVersion.empty());
    savedHash = versions.getVersion(savedVersion)->stateHash;
    savedBlob = state.buildStateBlob(f.mixer);
    state.publishBlob(savedBlob);
    fiddle::StateSharedMemory host(false, stateFile);
    REQUIRE(host.pullState() == savedBlob);

    // A second isolated endpoint must neither read nor overwrite this host.
    fiddle::StateSharedMemory otherHost(false, otherStateFile);
    REQUIRE(!otherHost.isReady());
    fiddle::StateSharedMemory otherProducer(true, otherStateFile);
    const juce::MemoryBlock sentinel("other", 5);
    otherProducer.pushState(sentinel);
    REQUIRE(otherHost.pullState() == sentinel);
    REQUIRE(host.pullState() == savedBlob);

    REQUIRE(f.commands.removeGroupBus(bus->id));
    f.mixer.getStrip(a)->setMuted(false);
    REQUIRE(state.commitCurrentState(f.mixer, branch) != savedVersion);
  } // Close SQLite and destroy every mixer/processor before reading again.
  {
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(databaseFile));
    fiddle::versioning::VersionStore versions(*db.getVersionStorage());
    const auto target = versions.resolveProjectRestoreTarget(branch, savedVersion, "");
    REQUIRE(target && target->versionId == savedVersion);
    REQUIRE(versions.getVersion(savedVersion)->stateHash == savedHash);
    const auto state = versions.getState(savedHash);
    REQUIRE(state && state->globalState.groupBuses.size() == 1);
    const auto &bus = state->globalState.groupBuses.front();
    REQUIRE(bus.id == busId && bus.name == "Saved Strings");
    REQUIRE(bus.gainDb == -3.0f && bus.muted && bus.soloed);
    REQUIRE(state->globalState.masterGainDb == -6.0f);
    const auto rack = fiddle::deserializeStripAudioSnapshot(
        bus.audioInsertState.data(), bus.audioInsertState.size());
    REQUIRE(rack.preFaderInserts.size() == 1 && rack.postFaderInserts.size() == 1);
    REQUIRE(rack.preFaderInserts.front().slotId == "pre");
    REQUIRE(rack.postFaderInserts.front().slotId == "post");
    REQUIRE(rack.postFaderInserts.front().bypassed);
    float savedAmount = 0;
    const auto &pluginState = rack.preFaderInserts.front().pluginState;
    REQUIRE(pluginState.getSize() == sizeof(savedAmount));
    std::memcpy(&savedAmount, pluginState.getData(), sizeof(savedAmount));
    REQUIRE(savedAmount == 2.0f);
    REQUIRE(state->stripHashes.size() == 2);
    const auto first = versions.getStripBlob(state->stripHashes[0]);
    const auto second = versions.getStripBlob(state->stripHashes[1]);
    REQUIRE(first && second);
    REQUIRE(first->directOutputBusId == busId && second->directOutputBusId == busId);
    REQUIRE(first->muted && second->gainDb == -9.0f);

    const auto restored = fiddle::StateManager::deserializeBlob(
        savedBlob.getData(), savedBlob.getSize());
    REQUIRE(restored && restored->stateHash == savedHash);
    REQUIRE(!restored->ancestorHashes.empty());
    REQUIRE(restored->ancestorHashes.back() == savedVersion);
    REQUIRE(restored->strips.size() == 2 && restored->strips.front().muted);
  }
}

void testHistoricalMixerSaveForkSurvivesReopen() {
  using Kind = fiddle::versioning::ProjectSaveResult::Kind;
  Sandbox sandbox;
  const auto databaseFile = sandbox.directory.getChildFile("fork.sqlite");
  fiddle::versioning::ProjectSaveResult a, b, fork, continued;
  std::string main;
  {
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(databaseFile));
    fiddle::versioning::VersionStore versions(*db.getVersionStorage());
    const auto root = versions.initializeEmpty();
    main = versions.getVersion(root)->branchId;
    MixerFixture f;
    const auto strip = f.instrument(0.1f, 1);
    REQUIRE(f.commands.addGroupBus("Strings", {strip}));
    addEffect(f.mixer.getAllGroupBuses().front()->audioEngine(), "double", 2.0f);
    fiddle::StateManager state;
    state.setVersionStore(&versions);
    state.initialize(sandbox.directory.getChildFile("state.bin"));
    a = state.saveCurrentState(f.mixer, main, root);
    REQUIRE(a.kind == Kind::Committed);
    f.mixer.getStrip(strip)->setGainDb(-3.0f);
    b = state.saveCurrentState(f.mixer, main, a.versionId);
    REQUIRE(b.kind == Kind::Committed);

    // Put the live mixer back at A. A dirty hint following edit/undo must not
    // create a branch when the actual captured state equals A.
    f.mixer.getStrip(strip)->setGainDb(0.0f);
    state.markDirty();
    const auto unchanged = state.saveCurrentState(f.mixer, main, a.versionId);
    REQUIRE(unchanged.kind == Kind::Unchanged);
    REQUIRE(unchanged.versionId == a.versionId);
    REQUIRE(versions.getBranchHead(main) == b.versionId);

    f.mixer.getStrip(strip)->setGainDb(-6.0f);
    fork = state.saveCurrentState(f.mixer, main, a.versionId);
    REQUIRE(fork.kind == Kind::Branched);
    REQUIRE(versions.getVersion(fork.versionId)->parentId == a.versionId);
    REQUIRE(versions.getBranchHead(main) == b.versionId);
    REQUIRE(state.saveCurrentState(f.mixer, fork.branchId, fork.versionId).kind == Kind::Unchanged);
    f.mixer.getStrip(strip)->setMuted(true);
    continued = state.saveCurrentState(f.mixer, fork.branchId, fork.versionId);
    REQUIRE(continued.kind == Kind::Committed);
    REQUIRE(continued.branchId == fork.branchId);
    state.setCurrentBranchId(continued.branchId);
    state.setConfigName(juce::String(continued.branchName));
    const auto blob = state.buildStateBlob(f.mixer);
    state.publishBlob(blob);
    const auto decoded = fiddle::StateManager::deserializeBlob(blob.getData(), blob.getSize());
    REQUIRE(decoded && decoded->ancestorHashes.back() == continued.versionId);
    REQUIRE(decoded->stateHash == versions.getVersion(continued.versionId)->stateHash);
  }
  {
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(databaseFile));
    fiddle::versioning::VersionStore versions(*db.getVersionStorage());
    REQUIRE(versions.getBranchHead(main) == b.versionId);
    REQUIRE(versions.getBranchHead(fork.branchId) == continued.versionId);
    const auto restoredA = versions.resolveProjectRestoreTarget(
        continued.branchId, continued.versionId, "");
    const auto restoredB = versions.resolveProjectRestoreTarget(main, b.versionId, "");
    REQUIRE(restoredA && restoredA->versionId == continued.versionId);
    REQUIRE(restoredB && restoredB->versionId == b.versionId);
    REQUIRE(versions.getVersion(fork.versionId)->parentId == a.versionId);
    const auto saved = versions.getState(versions.getVersion(continued.versionId)->stateHash);
    REQUIRE(saved && saved->globalState.groupBuses.size() == 1);
    const auto strip = versions.getStripBlob(saved->stripHashes.front());
    REQUIRE(strip && strip->muted && strip->gainDb == -6.0f);
    REQUIRE(strip->directOutputBusId == saved->globalState.groupBuses.front().id);
  }
}

std::vector<float> renderPassage(MixerFixture &f) {
  for (int channel = 1; channel <= 3; ++channel)
    f.noteOn(channel);
  std::vector<float> samples;
  for (int block = 0; block < 8; ++block) {
    const auto audio = f.render();
    for (int channel = 0; channel < 2; ++channel)
      samples.insert(samples.end(), audio.getReadPointer(channel),
                     audio.getReadPointer(channel) + audio.getNumSamples());
  }
  return samples;
}

void requireSameAudio(const std::vector<float> &a, const std::vector<float> &b) {
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i)
    REQUIRE(std::abs(a[i] - b[i]) < 0.00001f);
}

fiddle::ProjectRestoreService::Callbacks restoreCallbacks(int &finished) {
  fiddle::ProjectRestoreService::Callbacks callbacks;
  callbacks.findInstrument = [](int uid) -> std::optional<juce::PluginDescription> {
    return uid == 101 ? std::optional{description(true)} : std::nullopt;
  };
  callbacks.finished = [&finished] { ++finished; };
  return callbacks;
}

void testRestoreRecreatesAudioAfterDatabaseReopen() {
  Sandbox sandbox;
  const auto file = sandbox.directory.getChildFile("restore.sqlite");
  std::string version, branch, busId;
  std::vector<juce::String> ids;
  std::vector<float> expected;
  {
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(file));
    auto *repository = db.getLibraryRoutingRepository();
    fiddle::versioning::VersionStore versions(*db.getVersionStorage());
    const auto root = versions.initializeEmpty();
    branch = versions.getVersion(root)->branchId;
    MixerFixture f;
    for (int i = 1; i <= 3; ++i) {
      ids.push_back(f.instrument(0.1f * i, i));
      fiddle::ChairRow chair;
      chair.id = "chair-" + std::to_string(i);
      chair.instrumentEntityId = "test.violin";
      chair.name = "Violin " + std::to_string(i);
      chair.family = "strings";
      chair.ordinal = i;
      chair.displayOrder = i;
      chair.flatIndex = i;
      REQUIRE(repository->upsertChair(chair));
      fiddle::LayerRow layer;
      layer.id = ids.back().toStdString();
      layer.chairId = chair.id;
      layer.patchId = "deleted-catalog-patch";
      layer.patchName = "Historical violin";
      layer.libraryName = "Saved library";
      layer.sourcePatchRevision = 4;
      REQUIRE(repository->upsertLayer(layer));
      auto *strip = f.mixer.getStrip(ids.back());
      strip->chairId = chair.id;
      strip->patchId = layer.patchId;
      strip->layerName = layer.patchName;
      strip->library = layer.libraryName;
      strip->family = chair.family;
    }
    REQUIRE(f.commands.addGroupBus("Strings", {ids[0], ids[1]}));
    auto *bus = f.mixer.getAllGroupBuses().front();
    busId = bus->id.toStdString();
    bus->setGainDb(-3.0f);
    addEffect(bus->audioEngine(), "bus-pre", 2.0f);
    addEffect(bus->audioEngine(), "bus-post", 0.75f, 0, fiddle::StripInsertPosition::postFader);
    addEffect(f.mixer.getStrip(ids[0])->audioEngine(), "strip-pre", 0.5f);
    addEffect(f.mixer.getStrip(ids[0])->audioEngine(), "strip-bypassed", 4.0f,
               0, fiddle::StripInsertPosition::postFader);
    REQUIRE(f.mixer.getStrip(ids[0])->audioEngine().setBypassed("strip-bypassed", true));
    f.mixer.getStrip(ids[1])->setGainDb(-6.0f);
    f.mixer.getStrip(ids[2])->setMuted(true);
    fiddle::MasterInsertSnapshot master;
    master.slotId = "master-effect";
    master.description = description(false);
    REQUIRE(f.mixer.masterAudio().insertProcessor(
        master, 0, std::make_unique<SignalProcessor>(false, 0.5f)));
    f.mixer.masterAudio().setGainDb(-2.0f);
    expected = renderPassage(f);
    REQUIRE(*std::max_element(expected.begin(), expected.end()) > 0.01f);
    for (auto *strip : f.mixer.getAllStrips())
      strip->refreshPluginStateCache();
    fiddle::StateManager state;
    state.setVersionStore(&versions);
    state.setRoutingRepository(repository);
    version = state.commitCurrentState(f.mixer, branch);
  }
  {
    // Fresh mixer, processors and SQLite connection; no source instances survive.
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(file));
    fiddle::versioning::VersionStore versions(*db.getVersionStorage());
    const auto saved = versions.getState(versions.getVersion(version)->stateHash);
    REQUIRE(saved && saved->routingState.schemaVersion == 2);
    MixerFixture f;
    auto format = std::make_unique<TestPluginFormat>();
    auto *factory = format.get();
    f.mixer.getFormatManager().addFormat(std::move(format));
    int finished = 0;
    fiddle::ProjectRestoreService restore(f.mixer, versions,
        db.getLibraryRoutingRepository(), restoreCallbacks(finished));
    const auto result = restore.restore(*saved);
    REQUIRE(result.accepted && result.restoredTopology);
    REQUIRE(restore.isLoading());
    REQUIRE(finished == 0);
    pumpUntil([&] { return !restore.isLoading(); });
    REQUIRE(finished == 1);
    REQUIRE(f.mixer.size() == 3);
    REQUIRE(f.mixer.getStrip(ids[0])->directOutputBusId.toStdString() == busId);
    REQUIRE(f.mixer.getStrip(ids[0])->layerName == "Historical violin");
    REQUIRE(f.mixer.getStrip(ids[0])->library == "Saved library");
    REQUIRE(f.mixer.getStrip(ids[0])->missingPatchReference);
    REQUIRE(f.mixer.getStrip(ids[2])->realtimeState().muted);
    REQUIRE(db.getLibraryRoutingRepository()->getLayer(ids[0].toStdString())->sourcePatchRevision == 4);
    requireSameAudio(expected, renderPassage(f));
    fiddle::StateManager state;
    state.setVersionStore(&versions);
    state.setRoutingRepository(db.getLibraryRoutingRepository());
    for (auto *strip : f.mixer.getAllStrips())
      strip->refreshPluginStateCache();
    REQUIRE(state.saveCurrentState(f.mixer, branch, version).kind ==
            fiddle::versioning::ProjectSaveResult::Kind::Unchanged);

    auto invalid = *saved;
    invalid.routingState.layers.front().stripHash = "missing-blob";
    REQUIRE(!restore.restore(invalid).accepted);
    invalid = *saved;
    invalid.routingState.layers.front().chairId = "missing-chair";
    REQUIRE(!restore.restore(invalid).accepted);
    REQUIRE(f.mixer.size() == 3 && finished == 1);
    REQUIRE(db.getLibraryRoutingRepository()->listLayers().size() == 3);

    // Reuse stable layer IDs in overlapping restores, with different state.
    // Deliver the newer loads first, then obsolete callbacks from the old one.
    factory->defer = true;
    REQUIRE(restore.restore(*saved).accepted);
    pumpUntil([&] { return factory->pending.size() == 8; });
    auto newer = *saved;
    const auto oldHash = newer.routingState.layers.front().stripHash;
    auto changed = *versions.getStripBlob(oldHash);
    const float louder = 0.4f;
    changed.pluginState.resize(sizeof(louder));
    std::memcpy(changed.pluginState.data(), &louder, sizeof(louder));
    const auto newHash = changed.computeHash();
    versions.getStorage().putStripBlob(newHash, changed);
    newer.routingState.layers.front().stripHash = newHash;
    std::replace(newer.stripHashes.begin(), newer.stripHashes.end(), oldHash, newHash);
    REQUIRE(restore.restore(newer).accepted);
    pumpUntil([&] { return factory->pending.size() == 16; });
    std::reverse(factory->pending.begin(), factory->pending.end());
    factory->completeAll();
    pumpUntil([&] { return !restore.isLoading(); });
    REQUIRE(finished == 2);
    REQUIRE(f.mixer.getStrip(ids[0])->cachedPluginState() ==
            juce::MemoryBlock(&louder, sizeof(louder)));
  }
}

void testMissingPluginsAndSupersededRestore() {
  Sandbox sandbox;
  fiddle::FiddleDatabase db;
  REQUIRE(db.open(sandbox.directory.getChildFile("missing.sqlite")));
  fiddle::versioning::VersionStore versions(*db.getVersionStorage());
  const auto root = versions.initializeEmpty();
  const auto branch = versions.getVersion(root)->branchId;
  fiddle::versioning::FiddleState saved;
  std::vector<float> expected;
  {
    MixerFixture source;
    const auto id = source.instrument(0.1f, 1);
    addEffect(source.mixer.getStrip(id)->audioEngine(), "effect", 2.0f);
    expected = renderPassage(source);
    source.mixer.getStrip(id)->refreshPluginStateCache();
    fiddle::StateManager state;
    state.setVersionStore(&versions);
    const auto version = state.commitCurrentState(source.mixer, branch);
    saved = *versions.getState(versions.getVersion(version)->stateHash);
  }
  MixerFixture f;
  auto format = std::make_unique<TestPluginFormat>();
  auto *factory = format.get();
  factory->missingInstruments = true;
  factory->missingEffects = true;
  f.mixer.getFormatManager().addFormat(std::move(format));
  int finished = 0;
  fiddle::ProjectRestoreService restore(f.mixer, versions, nullptr, restoreCallbacks(finished));
  REQUIRE(restore.restore(saved).accepted);
  pumpUntil([&] { return finished == 1; });
  auto *strip = f.mixer.getAllStrips().front();
  REQUIRE(strip->pluginStatus() == fiddle::HostedPluginStatus::missing);
  const auto blob = versions.getStripBlob(saved.stripHashes.front());
  REQUIRE(strip->cachedPluginState() == juce::MemoryBlock(blob->pluginState.data(), blob->pluginState.size()));
  const auto rack = fiddle::deserializeStripAudioSnapshot(blob->audioInsertState.data(), blob->audioInsertState.size());
  REQUIRE(strip->audioEngine().snapshot("effect")->pluginState == rack.preFaderInserts.front().pluginState);
  for (const float sample : renderPassage(f))
    REQUIRE(sample == 0.0f);

  factory->missingInstruments = false;
  REQUIRE(restore.restore(saved).accepted);
  pumpUntil([&] { return finished == 2; });
  // Missing effect is retained as pass-through, not a dead audio path.
  for (const float sample : renderPassage(f))
    REQUIRE(std::abs(sample - 0.1f) < 0.00001f);

  factory->missingEffects = false;
  factory->defer = true;
  REQUIRE(restore.restore(saved).accepted);
  pumpUntil([&] { return factory->pending.size() == 2; });
  // Replace a project before its asynchronous instrument/effect creation completes.
  REQUIRE(restore.restore(saved).accepted);
  pumpUntil([&] { return factory->pending.size() == 4; });
  factory->completeAll();
  pumpUntil([&] { return !restore.isLoading(); });
  REQUIRE(finished == 3); // Superseded restore must not publish/persist.
  requireSameAudio(expected, renderPassage(f));

  REQUIRE(restore.restore(saved).accepted);
  pumpUntil([&] { return factory->pending.size() == 2; });
  // Removing a loading strip discards its instrument callback. The completion
  // barrier must still release once JUCE disposes of that cancelled request.
  f.mixer.clear();
  factory->completeAll();
  pumpUntil([&] { return !restore.isLoading(); });
  REQUIRE(finished == 4 && f.mixer.size() == 0);

  {
    fiddle::ProjectRestoreService abandoned(f.mixer, versions, nullptr, restoreCallbacks(finished));
    REQUIRE(abandoned.restore(saved).accepted);
    pumpUntil([&] { return factory->pending.size() == 2; });
  }
  factory->completeAll();
  juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  REQUIRE(finished == 4);
}

void testCancelledStateRebuildRetainsPublishedBlob() {
  Sandbox sandbox;
  const auto file = sandbox.directory.getChildFile("state.bin");
  fiddle::StateManager state;
  state.initialize(file);
  const juce::MemoryBlock original("saved-project", 13);
  state.publishBlob(original);
  fiddle::StateSharedMemory host(false, file);
  bool called = false;
  state.scheduleRebuild([&] { called = true; return juce::MemoryBlock{}; });
  pumpUntil([&] { return called; });
  REQUIRE(host.pullState() == original);
  const juce::MemoryBlock replacement("next-project", 12);
  state.scheduleRebuild([&] { return replacement; });
  pumpUntil([&] { return host.pullState() == replacement; });
}

} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI juce;
  int failed = 0;
  const auto run = [&](const char *name, auto test) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
      ++failed;
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
  };
  run("production routing and audibility", testProductionRoutingAndAudibility);
  run("meter snapshots avoid plugin queries and state capture", testMeterOnlySnapshots);
  run("plugin timings and buffer changes cover instruments and every FX path", testPluginTimingAndReprepare);
  run("UI bus removal and undo/redo", testBusRemovalThroughUiCommandsAndUndo);
  run("sample alignment through strip and bus latency", testRealSampleLatencyAcrossDirectAndBusRoutes);
  run("panic clears sounding and future notes", testPanicClearsSoundingAndFutureNotes);
  run("graceful stop tolerates incomplete note tracking", testGracefulStopWithIncompleteNoteTracking);
  run("saved routing survives database reopen", testSnapshotSurvivesDatabaseReopenAndStateExchange);
  run("historical mixer save forks and survives reopen", testHistoricalMixerSaveForkSurvivesReopen);
  run("restore recreates audio after database reopen", testRestoreRecreatesAudioAfterDatabaseReopen);
  run("missing plug-ins and superseded restore", testMissingPluginsAndSupersededRestore);
  run("cancelled rebuild retains published host state", testCancelledStateRebuildRetainsPublishedBlob);
  return failed == 0 ? 0 : 1;
}
