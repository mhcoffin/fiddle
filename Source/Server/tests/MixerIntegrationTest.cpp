#include "FiddleDatabase.h"
#include "ExpressionMapCommandService.h"
#include "ExpressionMapLibrary.h"
#include "ChairLayerActions.h"
#include "LayerLibrarySetupAction.h"
#include "LibraryCatalogAction.h"
#include "LibraryPatchPreviewHost.h"
#include "ChairActions.h"
#include "StripAudioActions.h"
#include "MasterAudioActions.h"
#include "GroupBusActions.h"
#include "GroupBusCommandService.h"
#include "GroupBusJsHandlers.h"
#include "MessageRouter.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "ProjectRestoreService.h"
#include "StateManager.h"
#include "StateBlobTypes.h"
#include "UndoManager.h"
#include "UndoActions.h"
#include "ProjectSettingsAction.h"
#include "MixerControlActions.h"
#include "RenderAheadEngine.h"
#include "NativePlugin/AudioConsumer.h"

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
  inline static std::atomic<SignalProcessor *> lastCreated{nullptr};
  inline static int programQueries = 0;
  inline static int stateCaptures = 0;
  inline static std::vector<std::pair<double, int>> preparations;
  bool counting = false;
  std::optional<juce::PluginDescription> requestedDescription;
  uint64_t frames = 0;
  std::function<void()> onProcess, onDestroy;
  ~SignalProcessor() override { if (onDestroy) onDestroy(); }
  SignalProcessor(bool instrument, float amount, int latency = 0)
      : AudioPluginInstance(instrument
                           ? BusesProperties().withOutput(
                                 "Output", juce::AudioChannelSet::stereo(), true)
                           : BusesProperties()
                                 .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                 .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        instrument_(instrument), amount_(amount), latency_(latency) {
    lastCreated.store(this);
    setLatencySamples(latency);
  }
  const juce::String getName() const override { return "Deterministic signal"; }
  void fillInPluginDescription(juce::PluginDescription &result) const override {
    result = requestedDescription.value_or(description(instrument_));
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
    if (onProcess) onProcess();
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
        float value = instrument_ ? (counting ? amount_ * float(frames + sample + 1) : playing ? amount_ : 0.0f)
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
    frames += audio.getNumSamples();
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
      else {
        auto processor = std::make_unique<SignalProcessor>(desc.isInstrument, 0.9375f);
        processor->requestedDescription = desc;
        callback(std::move(processor), {});
      }
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
  // Scalar/session persistence may use cached bytes. Explicit saves first
  // refresh every cache under the processing gate, then use this path.
  (void)f.mixer.masterAudio().snapshotAll(false);
  (void)f.mixer.getStrip(a)->audioEngine().snapshotAll(false);
  (void)bus->audioEngine().snapshotAll(false);
  REQUIRE(SignalProcessor::stateCaptures == captures);

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

juce::String expressionMapXml(const juce::String &name,
                              const juce::String &entityId) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<kScoreLibrary><expressionMapDefinitions><entities array=\"true\">"
         "<ExpressionMapDefinition><name>" + name + "</name><entityID>" +
         entityId +
         "</entityID><playingTechniqueCombinations array=\"true\">"
         "<playingTechniqueCombination><name>Natural</name>"
         "<baseSwitchID>1</baseSwitchID><techniqueIDs>pt.natural</techniqueIDs>"
         "<switchOnActions array=\"true\"/><switchOffActions array=\"true\"/>"
         "</playingTechniqueCombination></playingTechniqueCombinations>"
         "</ExpressionMapDefinition></entities></expressionMapDefinitions>"
         "</kScoreLibrary>";
}

void testExpressionMapUndoAndImportedPersistence() {
  Sandbox sandbox;
  const auto catalogFile = sandbox.directory.getChildFile("catalog.doricolib");
  const auto importedFile = sandbox.directory.getChildFile("imported.doricolib");
  const auto invalidFile = sandbox.directory.getChildFile("invalid.doricolib");
  const auto catalogXml = expressionMapXml("Catalog map", "map.catalog");
  const auto importedXml = expressionMapXml("Imported map", "map.imported");
  REQUIRE(catalogFile.replaceWithText(catalogXml));
  REQUIRE(importedFile.replaceWithText(importedXml));
  REQUIRE(invalidFile.replaceWithText("not an expression map"));

  fiddle::ExpressionMapLibrary library;
  library.addFile(catalogFile);
  MixerFixture f;
  const auto firstId = f.mixer.addStrip();
  const auto secondId = f.mixer.addStrip();
  fiddle::ExpressionMapCommandService commands(f.mixer, library, f.undo);

  REQUIRE(commands.assign(firstId, "map.catalog"));
  auto *first = f.mixer.getStrip(firstId);
  const auto catalogMap = first->expressionMap;
  REQUIRE(catalogMap && catalogMap->name == "Catalog map");
  REQUIRE(first->expressionMapPath.isEmpty());
  f.undo.markSavePoint();

  REQUIRE(commands.importFile(firstId, importedFile));
  const auto importedMap = first->expressionMap;
  REQUIRE(importedMap && importedMap->name == "Imported map");
  REQUIRE(first->expressionMapPath == importedFile.getFullPathName());
  REQUIRE(first->expressionMapSourceXml == importedXml);
  REQUIRE(!commands.importFile(firstId, importedFile));

  // History owns both sides. Neither Undo nor Redo may consult source files or
  // a changed catalog after the command has been accepted.
  REQUIRE(catalogFile.deleteFile());
  REQUIRE(importedFile.deleteFile());
  REQUIRE(f.undo.undo());
  REQUIRE(first->expressionMap == catalogMap);
  REQUIRE(first->expressionMapPath.isEmpty());
  REQUIRE(first->expressionMapSourceXml.isEmpty());
  REQUIRE(f.undo.isAtSavePoint());
  REQUIRE(f.undo.redo());
  REQUIRE(first->expressionMap == importedMap);
  REQUIRE(first->expressionMapPath.endsWith("imported.doricolib"));
  REQUIRE(first->expressionMapSourceXml == importedXml);

  REQUIRE(commands.clear(firstId));
  REQUIRE(!first->expressionMap && first->expressionMapPath.isEmpty() &&
          first->expressionMapSourceXml.isEmpty());
  REQUIRE(f.undo.undo());
  REQUIRE(first->expressionMap == importedMap);
  REQUIRE(first->expressionMapSourceXml == importedXml);

  // Parse failures and an invalid group target are atomic: no state or history
  // moves, including when another selected strip would otherwise be changed.
  f.undo.clear();
  REQUIRE(!commands.importFile(firstId, invalidFile));
  REQUIRE(!f.undo.canUndo() && first->expressionMap == importedMap);
  REQUIRE(!commands.assignGroup({firstId, "missing-strip"}, ""));
  REQUIRE(first->expressionMap == importedMap);
  REQUIRE(!f.undo.canUndo());
  REQUIRE(commands.assignGroup({firstId, secondId}, ""));
  REQUIRE(!first->expressionMap && !f.mixer.getStrip(secondId)->expressionMap);
  REQUIRE(f.undo.undo());
  REQUIRE(first->expressionMap == importedMap);

  // Session rows, compatibility blobs, and versioned strip blobs all retain
  // the original XML. Restoration therefore succeeds after the file is gone.
  const auto databaseFile = sandbox.directory.getChildFile("maps.sqlite");
  fiddle::FiddleDatabase db;
  REQUIRE(db.open(databaseFile));
  db.saveStrip(*first, 0);
  db.close();
  REQUIRE(db.open(databaseFile));
  const auto rows = db.loadAllStrips();
  REQUIRE(rows.size() == 1);
  REQUIRE(rows[0].expressionMapPath == first->expressionMapPath);
  REQUIRE(rows[0].expressionMapSourceXml == importedXml);

  fiddle::versioning::VersionStore versions(*db.getVersionStorage());
  const auto root = versions.initializeEmpty();
  const auto branch = versions.getVersion(root)->branchId;
  fiddle::StateManager state;
  state.setVersionStore(&versions);
  state.initialize(sandbox.directory.getChildFile("state.bin"));
  const auto saved = state.saveCurrentState(f.mixer, branch, root);
  REQUIRE(saved.kind == fiddle::versioning::ProjectSaveResult::Kind::Committed);
  const auto version = versions.getVersion(saved.versionId);
  REQUIRE(version.has_value());
  const auto savedState = versions.getState(version->stateHash);
  REQUIRE(savedState.has_value() && !savedState->stripHashes.empty());
  const auto blob = versions.getStripBlob(savedState->stripHashes.front());
  REQUIRE(blob.has_value());
  REQUIRE(blob->expressionMapPath == first->expressionMapPath.toStdString());
  REQUIRE(blob->expressionMapSourceXml == importedXml.toStdString());

  const auto compatibility = state.buildStateBlob(f.mixer);
  const auto decoded = fiddle::deserializeStateBlob(
      compatibility.getData(), compatibility.getSize());
  REQUIRE(decoded.has_value() && !decoded->strips.empty());
  REQUIRE(decoded->strips.front().expressionMapPath == first->expressionMapPath);
  REQUIRE(decoded->strips.front().expressionMapSourceXml == importedXml);

  MixerFixture restored;
  int finished = 0;
  fiddle::ProjectRestoreService::Callbacks callbacks;
  callbacks.loadMap = [&library](const std::string &id,
                                 const juce::String &sourceXml) {
    return library.loadPersisted(id, sourceXml);
  };
  callbacks.finished = [&finished] { ++finished; };
  fiddle::ProjectRestoreService restore(restored.mixer, versions, nullptr,
                                        std::move(callbacks));
  REQUIRE(restore.restore(*savedState).accepted);
  pumpUntil([&] { return !restore.isLoading(); });
  REQUIRE(finished == 1 && restored.mixer.size() == 2);
  auto *restoredFirst = restored.mixer.getAllStrips().front();
  REQUIRE(restoredFirst->expressionMap);
  REQUIRE(restoredFirst->expressionMap->name == "Imported map");
  REQUIRE(restoredFirst->expressionMapPath.endsWith("imported.doricolib"));
  REQUIRE(restoredFirst->expressionMapSourceXml == importedXml);
}

float stateAmount(const juce::MemoryBlock &state) {
  REQUIRE(state.getSize() == sizeof(float));
  float result = 0; std::memcpy(&result, state.getData(), sizeof(result));
  return result;
}

void testFrozenSaveCapturesEachPluginOnce() {
  Sandbox sandbox;
  fiddle::FiddleDatabase db;
  REQUIRE(db.open(sandbox.directory.getChildFile("single-capture.sqlite")));
  fiddle::versioning::VersionStore versions(*db.getVersionStorage());
  const auto root = versions.initializeEmpty();
  const auto branch = versions.getVersion(root)->branchId;

  MixerFixture f;
  const auto first = f.instrument(0.1f, 1);
  (void)f.instrument(0.2f, 2);
  REQUIRE(f.commands.addGroupBus("Frozen", {first}));
  addEffect(f.mixer.getStrip(first)->audioEngine(), "strip-frozen", 2.0f);
  addEffect(f.mixer.getAllGroupBuses().front()->audioEngine(), "bus-frozen", 0.5f);
  fiddle::MasterInsertSnapshot master;
  master.slotId = "master-frozen";
  master.description = description(false);
  REQUIRE(f.mixer.masterAudio().insertProcessor(
      master, 0, std::make_unique<SignalProcessor>(false, 1.0f)));

  const auto before = SignalProcessor::stateCaptures;
  f.mixer.capturePluginStateCachesForSave();
  REQUIRE(SignalProcessor::stateCaptures == before + 5); // two instruments, three FX

  // Session/database serialization and the version commit both consume the
  // frozen caches. Neither may re-enter vendor getStateInformation().
  for (auto *strip : f.mixer.getAllStrips()) {
    REQUIRE(!strip->cachedPluginState().isEmpty());
    (void)strip->audioEngine().snapshotAll(false);
  }
  (void)f.mixer.getAllGroupBuses().front()->audioEngine().snapshotAll(false);
  (void)f.mixer.masterAudio().snapshotAll(false);
  fiddle::StateManager state;
  state.setVersionStore(&versions);
  state.initialize(sandbox.directory.getChildFile("state.bin"));
  const auto saved = state.saveCurrentState(
      f.mixer, branch, root, std::nullopt, false);
  REQUIRE(saved.kind == fiddle::versioning::ProjectSaveResult::Kind::Committed);
  REQUIRE(SignalProcessor::stateCaptures == before + 5);
}

void testLibraryCatalogHistory() {
  Sandbox sandbox;
  fiddle::FiddleDatabase db;
  REQUIRE(db.open(sandbox.directory.getChildFile("catalog-history.sqlite")));
  auto &repository = *db.getLibraryRoutingRepository();
  fiddle::UndoManager libraryHistory, projectHistory;
  fiddle::LibraryCatalogSnapshot snapshot;
  snapshot.id = "catalog"; snapshot.name = "Catalog"; snapshot.vendor = "Vendor";
  snapshot.exists = true;
  fiddle::LibraryPatchRow patch;
  patch.id = "patch"; patch.libraryId = snapshot.id; patch.pluginUid = 101;
  patch.pluginState = {1, 2, 3}; patch.name = "Original";
  snapshot.patches = {patch};
  REQUIRE(libraryHistory.perform(std::make_unique<fiddle::LibraryCatalogAction>(repository, snapshot)));
  REQUIRE(!libraryHistory.perform(std::make_unique<fiddle::LibraryCatalogAction>(repository, snapshot)));
  REQUIRE(libraryHistory.undo());
  REQUIRE(!repository.captureLibrary("catalog")->exists);
  REQUIRE(libraryHistory.redo());
  REQUIRE(repository.getPatch("patch")->pluginState == patch.pluginState);
  snapshot.patches[0].pluginState = {4, 5, 6};
  REQUIRE(libraryHistory.perform(std::make_unique<fiddle::LibraryCatalogAction>(repository, snapshot)));
  const auto savedRevision = repository.getPatch("patch")->revision;
  REQUIRE(libraryHistory.undo());
  REQUIRE(repository.getPatch("patch")->pluginState == patch.pluginState);
  REQUIRE(libraryHistory.redo());
  REQUIRE(repository.getPatch("patch")->revision == savedRevision);
  fiddle::LibraryCatalogSnapshot removed; removed.id = "catalog";
  REQUIRE(libraryHistory.perform(std::make_unique<fiddle::LibraryCatalogAction>(repository, removed)));
  REQUIRE(libraryHistory.undo());
  fiddle::ChairRow chair;
  chair.id = "chair"; chair.instrumentEntityId = "violin"; chair.name = "Violin"; chair.flatIndex = 1;
  REQUIRE(repository.upsertChair(chair));
  REQUIRE(repository.createLayerFromPatch("layer", "chair", "patch", 0));
  REQUIRE(!libraryHistory.redo());
  REQUIRE(libraryHistory.canRedo());
  REQUIRE(repository.getLayer("layer")->sourcePatchRevision == savedRevision);
  REQUIRE(repository.deleteLayer("layer"));
  REQUIRE(libraryHistory.redo());
  REQUIRE(libraryHistory.undo());
  REQUIRE(repository.getPatch("patch")->pluginState == snapshot.patches[0].pluginState);
  REQUIRE(!projectHistory.canUndo() && projectHistory.isAtSavePoint());
}

void testRetainedLibraryPreviews() {
  juce::AudioPluginFormatManager manager;
  auto *factory = new TestPluginFormat();
  manager.addFormat(std::unique_ptr<juce::AudioPluginFormat>(factory));
  fiddle::LibraryPatchPreviewHost host(manager);
  const float original = 0.125f;
  juce::MemoryBlock initial(&original, sizeof(original));
  bool completed = false;
  REQUIRE(host.open("a", "Test A", description(true), initial,
    [&](bool success, const juce::String &) { REQUIRE(success); completed = true; }));
  pumpUntil([&] { return completed; });
  auto *processor = SignalProcessor::lastCreated.load();
  const float edited = 0.75f;
  processor->setStateInformation(&edited, sizeof(edited));
  std::vector<std::uint8_t> captured;
  REQUIRE(host.captureState("a", 101, captured));
  REQUIRE(stateAmount(juce::MemoryBlock(captured.data(), captured.size())) == edited);
  host.seedState("copy", 101, captured);
  host.showOnly({"copy"}); // A deleted/replaced row is hidden, not destroyed.
  REQUIRE(host.size() == 1);
  completed = false;
  REQUIRE(host.open("a", "Restored A", description(true), {},
    [&](bool success, const juce::String &) { REQUIRE(success); completed = true; }));
  REQUIRE(completed && SignalProcessor::lastCreated.load() == processor);
  REQUIRE(host.captureState("a", 101, captured));
  REQUIRE(stateAmount(juce::MemoryBlock(captured.data(), captured.size())) == edited);
  const float later = 0.5f;
  processor->setStateInformation(&later, sizeof(later));
  REQUIRE(host.captureState("copy", 101, captured));
  REQUIRE(stateAmount(juce::MemoryBlock(captured.data(), captured.size())) == edited);
  REQUIRE(!host.captureState("a", 102, captured));
  host.seedState("empty", 101, {});
  REQUIRE(host.captureState("empty", 101, captured) && captured.empty());
  factory->defer = true;
  completed = false;
  REQUIRE(host.open("pending", "Pending", description(true), initial,
    [&](bool success, const juce::String &) { REQUIRE(success); completed = true; }));
  pumpUntil([&] { return !factory->pending.empty(); });
  REQUIRE(host.isLoading());
  REQUIRE(host.captureState("pending", 101, captured));
  REQUIRE(stateAmount(juce::MemoryBlock(captured.data(), captured.size())) == original);
  host.showOnly({"a"});
  REQUIRE(!host.isLoading());
  factory->completeAll();
  pumpUntil([&] { return completed; });
  REQUIRE(host.captureState("pending", 101, captured));
  REQUIRE(stateAmount(juce::MemoryBlock(captured.data(), captured.size())) == original);
  host.retainOnly({"a"});
  REQUIRE(host.size() == 1 && !host.captureState("copy", 101, captured));
  host.closeAll();
  REQUIRE(host.size() == 0 && !host.captureState("copy", 101, captured));
}

void testLayerLibrarySetupUndo() {
  Sandbox sandbox;
  fiddle::FiddleDatabase db;
  REQUIRE(db.open(sandbox.directory.getChildFile("layer-refresh.sqlite")));
  auto &repository = *db.getLibraryRoutingRepository();
  MixerFixture f;
  auto format = std::make_unique<TestPluginFormat>();
  auto *factory = format.get();
  f.mixer.getFormatManager().addFormat(std::move(format));
  const auto a = f.instrument(0.125f, 1), b = f.instrument(0.25f, 2);
  const auto unrelated = f.instrument(0.375f, 3);
  auto *first = f.mixer.getStrip(a);
  auto *second = f.mixer.getStrip(b);
  std::vector<fiddle::LayerLibrarySetup> targets;
  for (const auto &id : {a, b}) {
    fiddle::ChairRow chair;
    chair.id = "chair-" + id.toStdString(); chair.name = "Violin";
    chair.instrumentEntityId = "test.violin";
    REQUIRE(repository.insertChairWithStableAssignment(chair));
    auto *strip = f.mixer.getStrip(id);
    strip->chairId = chair.id; strip->patchId = "patch";
    strip->layerName = "Original " + id; strip->library = "Original library";
    fiddle::LayerRow row;
    row.id = id.toStdString(); row.chairId = chair.id; row.patchId = "patch";
    row.patchName = strip->layerName.toStdString(); row.sourcePatchRevision = 1;
    row.pluginUid = 101; row.pluginStateEdited = true;
    REQUIRE(repository.upsertLayer(row)); // Deliberately no cached plugin bytes.
    fiddle::LayerLibrarySetup target;
    target.row = row; target.row.patchName = "Updated patch";
    target.row.libraryName = "Updated library"; target.row.sourcePatchRevision = 2;
    target.row.pluginStateEdited = false;
    target.instrument.description = description(true);
    float amount = 0.5f;
    target.instrument.state.append(&amount, sizeof(amount));
    const auto *bytes = static_cast<const uint8_t *>(target.instrument.state.getData());
    target.row.pluginState.assign(bytes, bytes + sizeof(amount));
    targets.push_back(target);
  }
  first->setExpressionMap(std::make_shared<fiddle::ExpressionMapData>());
  const auto originalMap = first->expressionMap;
  first->expressionMapPath = "/historical/imported.doricolib";
  first->setGainDb(-7); first->setMuted(true); second->setActive(false);
  addEffect(first->audioEngine(), "unchanged-fx", 2.0f);
  REQUIRE(f.commands.addGroupBus("Strings", {a}));
  const auto output = first->directOutputBusId;
  f.undo.clear();
  int settled = 0;
  auto action = [&](std::vector<fiddle::LayerLibrarySetup> rows) {
    return std::make_unique<fiddle::LayerLibrarySetupAction>(repository, f.mixer,
        std::move(rows), "Update layers", [&](const juce::String &) { ++settled; });
  };
  REQUIRE(f.undo.perform(action(targets)));
  REQUIRE(settled == 2 && first == f.mixer.getStrip(a));
  REQUIRE(stateAmount(first->cachedPluginState()) == 0.5f);
  REQUIRE(stateAmount(second->cachedPluginState()) == 0.5f);
  REQUIRE(!first->expressionMap && first->expressionMapPath.isEmpty());
  REQUIRE(first->gainDb() == -7 && first->isMuted() && !second->isActive());
  REQUIRE(first->directOutputBusId == output && first->audioEngine().snapshot("unchanged-fx"));
  REQUIRE(repository.getLayer(a.toStdString())->sourcePatchRevision == 2);
  REQUIRE(f.undo.undo() && !f.undo.canUndo() && f.undo.isAtSavePoint());
  REQUIRE(stateAmount(first->cachedPluginState()) == 0.125f);
  REQUIRE(stateAmount(second->cachedPluginState()) == 0.25f);
  REQUIRE(first->expressionMap == originalMap && first->expressionMapPath == "/historical/imported.doricolib");
  REQUIRE(repository.getLayer(a.toStdString())->pluginStateEdited);
  REQUIRE(repository.getLayer(a.toStdString())->sourcePatchRevision == 1);
  // Redo never consults the source catalog. The action also captures departing
  // vendor edits afresh, rather than freezing the first Undo snapshot forever.
  float edited = 0.1875f;
  REQUIRE(first->applyPluginState(&edited, sizeof(edited)));
  REQUIRE(f.undo.redo());
  REQUIRE(stateAmount(first->cachedPluginState()) == 0.5f);
  edited = 0.625f;
  REQUIRE(second->applyPluginState(&edited, sizeof(edited)));
  REQUIRE(f.undo.undo());
  REQUIRE(stateAmount(first->cachedPluginState()) == 0.1875f);
  REQUIRE(f.undo.redo());
  REQUIRE(stateAmount(second->cachedPluginState()) == 0.625f);
  REQUIRE(stateAmount(f.mixer.getStrip(unrelated)->snapshotInstrument().state) == 0.375f);

  // Preflight rejects the whole command when one target disappeared.
  auto bad = targets;
  bad.back().row.id = "absent";
  const auto beforeRejected = first->snapshotInstrument().state;
  const int previousSettled = settled;
  REQUIRE(!f.undo.perform(action(bad)));
  REQUIRE(settled == previousSettled && first->snapshotInstrument().state == beforeRejected);
  REQUIRE(!f.undo.perform(action({})));

  // A failure on the second SQL update rolls back the first and never applies
  // either live preset, including when the failing operation is Undo.
  sqlite3 *raw = nullptr;
  REQUIRE(sqlite3_open(sandbox.directory.getChildFile("layer-refresh.sqlite")
                          .getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(raw, sqlite3_close);
  const auto trigger = "CREATE TRIGGER reject_setup BEFORE UPDATE ON layers WHEN NEW.id = '" +
      b.toStdString() + "' BEGIN SELECT RAISE(ABORT, 'test failure'); END";
  REQUIRE(sqlite3_exec(raw, trigger.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
  const auto otherBeforeRejected = second->snapshotInstrument().state;
  REQUIRE(!f.undo.undo());
  REQUIRE(f.undo.canUndo() && !f.undo.canRedo() && !f.undo.isAtSavePoint());
  REQUIRE(!f.undo.perform(action(targets)));
  REQUIRE(settled == previousSettled);
  REQUIRE(first->snapshotInstrument().state == beforeRejected);
  REQUIRE(second->snapshotInstrument().state == otherBeforeRejected);
  REQUIRE(repository.getLayer(a.toStdString())->sourcePatchRevision == 2);
  REQUIRE(repository.getLayer(b.toStdString())->sourcePatchRevision == 2);
  REQUIRE(sqlite3_exec(raw, "DROP TRIGGER reject_setup", nullptr, nullptr, nullptr) == SQLITE_OK);
  REQUIRE(f.undo.undo() && f.undo.isAtSavePoint());
  REQUIRE(f.undo.redo());

  // Changing player type uses real asynchronous hosting, with pending state
  // preserved when Undo arrives before instantiation completes.
  f.undo.clear();
  factory->defer = true;
  auto replacement = targets.front();
  replacement.instrument.description.uniqueId = 303;
  replacement.row.pluginUid = 303;
  REQUIRE(f.undo.perform(action({replacement})));
  pumpUntil([&] { return !factory->pending.empty(); });
  REQUIRE(first->requestedPluginUid() == 303);
  REQUIRE(f.undo.undo());
  pumpUntil([&] { return factory->pending.size() == 2; });
  factory->completeAll();
  pumpUntil([&] { return first->hasPlugin(); });
  REQUIRE(first->pluginUid == 101 && stateAmount(first->snapshotInstrument().state) == 0.5f);
  factory->defer = false;
  REQUIRE(f.undo.redo());
  pumpUntil([&] { return first->pluginStatus() != fiddle::HostedPluginStatus::loading; });
  REQUIRE(first->pluginUid == 303 && stateAmount(first->snapshotInstrument().state) == 0.5f);
  REQUIRE(f.undo.undo());
  pumpUntil([&] { return first->hasPlugin(); });
  // Failed plugin creation is an undoable missing placeholder, not state loss.
  factory->missingInstruments = true;
  REQUIRE(f.undo.redo());
  pumpUntil([&] { return first->pluginStatus() != fiddle::HostedPluginStatus::loading; });
  REQUIRE(!first->hasPlugin() && first->requestedPluginUid() == 303);
  REQUIRE(stateAmount(first->snapshotInstrument().state) == 0.5f);
  factory->missingInstruments = false;
  REQUIRE(f.undo.undo());
  pumpUntil([&] { return first->hasPlugin(); });
  REQUIRE(first->pluginUid == 101);

  // An update that intentionally clears the instrument remains reversible.
  f.undo.clear();
  auto empty = targets.front();
  empty.instrument = {};
  empty.row.pluginUid = 0;
  empty.row.pluginState.clear();
  REQUIRE(f.undo.perform(action({empty})));
  REQUIRE(!first->hasPlugin() && first->requestedPluginUid() == 0);
  REQUIRE(first->cachedPluginState().isEmpty());
  REQUIRE(f.undo.undo());
  pumpUntil([&] { return first->hasPlugin(); });
  REQUIRE(first->pluginUid == 101 && stateAmount(first->snapshotInstrument().state) == 0.5f);

  // Bypass remains a live layer control even while its new player is loading.
  f.undo.clear();
  factory->defer = true;
  REQUIRE(f.undo.perform(action({replacement})));
  pumpUntil([&] { return !factory->pending.empty(); });
  first->setPluginBypassed(true);
  factory->completeAll();
  pumpUntil([&] { return first->hasPlugin(); });
  REQUIRE(first->isPluginBypassed());
  factory->defer = false;
  REQUIRE(f.undo.undo());
  pumpUntil([&] { return first->hasPlugin(); });
  REQUIRE(!first->isPluginBypassed());
  REQUIRE(f.undo.redo());
  pumpUntil([&] { return first->hasPlugin(); });
  REQUIRE(first->isPluginBypassed());
}

void testInstrumentReplacementStateUndo() {
  MixerFixture f;
  auto format = std::make_unique<TestPluginFormat>();
  auto *factory = format.get();
  f.mixer.getFormatManager().addFormat(std::move(format));
  f.scanner.getKnownPluginListMutable().addType(description(true));
  const auto id = f.instrument(0.1f, 1);
  auto *strip = f.mixer.getStrip(id);
  strip->setPluginBypassed(true);
  auto settle = [&] { pumpUntil([&] { return strip->pluginStatus() != fiddle::HostedPluginStatus::loading; }); };
  REQUIRE(f.undo.perform(std::make_unique<fiddle::SetPluginAction>(f.mixer, f.scanner, id, 101, 0)));
  REQUIRE(!strip->hasPlugin());
  REQUIRE(f.undo.undo()); settle();
  REQUIRE(stateAmount(strip->cachedPluginState()) == 0.1f && strip->isPluginBypassed());
  float edited = 0.25f;
  REQUIRE(strip->applyPluginState(&edited, sizeof(edited)));
  REQUIRE(f.undo.redo() && !strip->hasPlugin());
  REQUIRE(f.undo.undo()); settle();
  REQUIRE(stateAmount(strip->cachedPluginState()) == edited);

  // Replacing with another UID preserves both independently edited sides.
  auto other = description(true); other.uniqueId = 201; other.name = "Other instrument";
  f.scanner.getKnownPluginListMutable().addType(other);
  REQUIRE(f.undo.perform(std::make_unique<fiddle::SetPluginAction>(f.mixer, f.scanner, id, 101, 201)));
  settle();
  float otherAmount = 0.375f;
  REQUIRE(strip->applyPluginState(&otherAmount, sizeof(otherAmount)));
  REQUIRE(f.undo.undo()); settle();
  REQUIRE(stateAmount(strip->cachedPluginState()) == edited);
  edited = 0.5f; REQUIRE(strip->applyPluginState(&edited, sizeof(edited)));
  REQUIRE(f.undo.redo()); settle();
  REQUIRE(stateAmount(strip->cachedPluginState()) == otherAmount);
  REQUIRE(f.undo.undo()); settle();
  REQUIRE(stateAmount(strip->cachedPluginState()) == edited);
  REQUIRE(strip->pluginUid == 101);
  REQUIRE(!f.undo.perform(std::make_unique<fiddle::SetPluginAction>(f.mixer, f.scanner, id, 101, 101)));
  REQUIRE(!f.undo.perform(std::make_unique<fiddle::SetPluginAction>(f.mixer, f.scanner, id, 101, 999)));
  REQUIRE(stateAmount(strip->cachedPluginState()) == edited);

  // Capture a pending restoration, cancel it, and complete stale callbacks.
  REQUIRE(f.undo.perform(std::make_unique<fiddle::SetPluginAction>(f.mixer, f.scanner, id, 101, 0)));
  factory->defer = true;
  REQUIRE(f.undo.undo());
  pumpUntil([&] { return !factory->pending.empty(); });
  REQUIRE(stateAmount(strip->cachedPluginState()) == edited);
  REQUIRE(f.undo.redo() && !strip->hasPlugin());
  factory->completeAll();
  REQUIRE(strip->pluginUid == 0 && !strip->hasPlugin());
  factory->defer = false;
  factory->missingInstruments = true;
  REQUIRE(f.undo.undo()); settle();
  REQUIRE(strip->pluginStatus() == fiddle::HostedPluginStatus::missing);
  REQUIRE(stateAmount(strip->cachedPluginState()) == edited);
  REQUIRE(f.undo.redo());
  factory->missingInstruments = false;
  f.scanner.getKnownPluginListMutable().clear(); // Undo does not depend on today's catalog.
  REQUIRE(f.undo.undo()); settle();
  REQUIRE(stateAmount(strip->cachedPluginState()) == edited);
}

void testFxStateUndo() {
  // Same production add/remove contract on layers, group buses and Master.
  for (int kind = 0; kind < 3; ++kind) {
    MixerFixture f;
    auto format = std::make_unique<TestPluginFormat>();
    auto *factory = format.get();
    f.mixer.getFormatManager().addFormat(std::move(format));
    const auto id = f.instrument(0.1f, 1);
    REQUIRE(f.commands.addGroupBus("Bus", {}));
    const auto bus = f.mixer.getAllGroupBuses().front()->id;
    f.undo.clear();
    auto *engine = kind == 0 ? &f.mixer.getStrip(id)->audioEngine() : &f.mixer.getGroupBus(bus)->audioEngine();
    auto snapshots = [&] {
      return kind == 2 ? f.mixer.masterAudio().snapshotAll().inserts : engine->snapshotAll().preFaderInserts;
    };
    std::unique_ptr<fiddle::UndoableAction> add;
    if (kind == 0) add = std::make_unique<fiddle::AddStripInsertAction>(f.mixer, id, description(false), fiddle::StripInsertPosition::preFader);
    if (kind == 1) add = std::make_unique<fiddle::AddGroupBusInsertAction>(f.mixer, bus, description(false), fiddle::StripInsertPosition::preFader);
    if (kind == 2) add = std::make_unique<fiddle::AddMasterInsertAction>(f.mixer, description(false));
    REQUIRE(f.undo.perform(std::move(add)));
    auto settle = [&] { pumpUntil([&] {
      return (kind == 2 ? f.mixer.masterAudio().toJson()["inserts"][0]["status"].toString() :
                         engine->toJson()["preFaderInserts"][0]["status"].toString()) == "loaded";
    }); };
    settle();
    const auto slot = snapshots()[0].slotId;
    float edited = 0.25f;
    SignalProcessor::lastCreated.load()->setStateInformation(&edited, sizeof(edited));
    REQUIRE(f.undo.undo() && snapshots().empty());
    REQUIRE(f.undo.redo());
    // Cached input bytes are present even BEFORE the async reload completes.
    REQUIRE(stateAmount(snapshots()[0].pluginState) == edited);
    settle();
    REQUIRE(stateAmount(snapshots()[0].pluginState) == edited);
    edited = 0.375f;
    SignalProcessor::lastCreated.load()->setStateInformation(&edited, sizeof(edited));
    REQUIRE(f.undo.undo() && snapshots().empty());
    REQUIRE(f.undo.redo()); settle();
    REQUIRE(stateAmount(snapshots()[0].pluginState) == edited);

    std::unique_ptr<fiddle::UndoableAction> remove;
    if (kind == 0) remove = std::make_unique<fiddle::RemoveStripInsertAction>(f.mixer, id, slot);
    if (kind == 1) remove = std::make_unique<fiddle::RemoveGroupBusInsertAction>(f.mixer, bus, slot);
    if (kind == 2) remove = std::make_unique<fiddle::RemoveMasterInsertAction>(f.mixer, slot);
    REQUIRE(f.undo.perform(std::move(remove)));
    REQUIRE(f.undo.undo());
    // Wait on the test factory's actual completion by draining message work.
    settle();
    edited = 0.5f;
    SignalProcessor::lastCreated.load()->setStateInformation(&edited, sizeof(edited));
    REQUIRE(f.undo.redo() && snapshots().empty());
    factory->defer = true;
    REQUIRE(f.undo.undo());
    pumpUntil([&] { return !factory->pending.empty(); });
    REQUIRE(stateAmount(snapshots()[0].pluginState) == edited);
    REQUIRE(f.undo.redo()); // Remove again while restoration is still loading.
    factory->completeAll();
    REQUIRE(snapshots().empty());
    factory->defer = false;
    REQUIRE(f.undo.undo());
    REQUIRE(stateAmount(snapshots()[0].pluginState) == edited);
    settle();
    REQUIRE(f.undo.redo());
    factory->missingEffects = true;
    REQUIRE(f.undo.undo());
    pumpUntil([&] {
      return (kind == 2 ? f.mixer.masterAudio().toJson()["inserts"][0]["status"].toString() :
                         engine->toJson()["preFaderInserts"][0]["status"].toString()) == "missing";
    });
    REQUIRE(stateAmount(snapshots()[0].pluginState) == edited);
    REQUIRE(f.undo.redo());
    factory->missingEffects = false;
    factory->defer = true;
    REQUIRE(f.undo.undo());
    pumpUntil([&] { return !factory->pending.empty(); });
    REQUIRE(kind == 2 ? f.mixer.masterAudio().setBypassed(slot, true) : engine->setBypassed(slot, true));
    factory->completeAll(); settle();
    REQUIRE(snapshots()[0].bypassed && stateAmount(snapshots()[0].pluginState) == edited);
  }
}

void testChairCommandsUndo() {
  Sandbox sandbox;
  fiddle::FiddleDatabase db;
  REQUIRE(db.open(sandbox.directory.getChildFile("chair-undo.sqlite")));
  auto &repository = *db.getLibraryRoutingRepository();
  MixerFixture f;
  int successes = 0, failures = 0;
  auto changed = [&](bool success) {
    if (success) ++successes; else ++failures;
  };
  auto add = [&](const std::string &id, fiddle::DoricoRole role) {
    fiddle::ChairRow row;
    row.id = id; row.name = id; row.instrumentEntityId = "test.violin";
    row.family = "strings"; row.role = role;
    REQUIRE(f.undo.perform(std::make_unique<fiddle::AddChairAction>(repository, f.mixer, row, changed)));
    return *repository.getChair(id);
  };
  const auto solo = add("solo", fiddle::DoricoRole::solo);
  const auto section = add("section", fiddle::DoricoRole::section);
  const auto second = add("section-2", fiddle::DoricoRole::section);
  REQUIRE(f.undo.undo() && !repository.getChair(second.id));
  REQUIRE(f.undo.redo());
  REQUIRE(repository.getChair(second.id)->flatIndex == second.flatIndex);
  REQUIRE(repository.getChair(second.id)->ordinal == second.ordinal);
  REQUIRE(repository.getChair(second.id)->displayOrder == second.displayOrder);
  // A restored chair must have consumed its tombstone, not collide with the
  // next new chair which asks for the same instrument and player type.
  const auto third = add("section-3", fiddle::DoricoRole::section);
  REQUIRE(third.flatIndex != second.flatIndex && third.ordinal > second.ordinal);

  // Empty chairs still belong to saved project topology. A rename creates a
  // distinct version; Undo returns to the exact original saved identity.
  fiddle::versioning::VersionStore versions(*db.getVersionStorage());
  const auto root = versions.initializeEmpty();
  const auto branch = versions.getVersion(root)->branchId;
  fiddle::StateManager state;
  state.setVersionStore(&versions);
  state.setRoutingRepository(&repository);
  const auto saved = state.commitCurrentState(f.mixer, branch);
  REQUIRE(f.undo.perform(std::make_unique<fiddle::EditChairsAction>(repository, f.mixer,
      std::vector<fiddle::EditChairsAction::Edit>{{section.id, "Temporary name", std::nullopt}}, changed)));
  REQUIRE(state.commitCurrentState(f.mixer, branch) != saved);
  REQUIRE(f.undo.undo());
  REQUIRE(state.saveCurrentState(f.mixer, branch, saved).kind ==
          fiddle::versioning::ProjectSaveResult::Kind::Unchanged);

  const auto id = f.instrument(0.1f, section.flatIndex % 16);
  auto *original = f.mixer.getStrip(id);
  original->chairId = section.id;
  original->isSolo = false;
  original->setGainDb(-7);
  original->setMuted(true);
  addEffect(original->audioEngine(), "unchanged-fx", 2.0f);
  f.undo.clear();
  using Edit = fiddle::EditChairsAction::Edit;
  REQUIRE(f.undo.perform(std::make_unique<fiddle::EditChairsAction>(repository, f.mixer,
      std::vector<Edit>{{section.id, "Renamed", fiddle::DoricoRole::solo},
                        {second.id, std::nullopt, fiddle::DoricoRole::solo}}, changed)));
  REQUIRE(repository.getChair(section.id)->ordinal == 2);
  REQUIRE(repository.getChair(second.id)->ordinal == 3);
  REQUIRE(repository.getChair(section.id)->flatIndex == section.flatIndex);
  REQUIRE(original->isSolo && original->gainDb() == -7 && original->isMuted());
  REQUIRE(f.mixer.getStrip(id) == original && original->audioEngine().snapshot("unchanged-fx"));
  REQUIRE(f.undo.undo() && !f.undo.canUndo()); // Both roles/name in one step.
  REQUIRE(repository.getChair(section.id)->name == section.name);
  REQUIRE(repository.getChair(section.id)->ordinal == section.ordinal);
  REQUIRE(repository.getChair(second.id)->ordinal == second.ordinal);
  REQUIRE(!original->isSolo && original->gainDb() == -7);
  REQUIRE(f.undo.redo() && repository.getChair(section.id)->name == "Renamed");
  const auto count = successes;
  REQUIRE(!f.undo.perform(std::make_unique<fiddle::EditChairsAction>(repository, f.mixer,
      std::vector<Edit>{{section.id, "Renamed", fiddle::DoricoRole::solo}}, changed)));
  REQUIRE(successes == count); // No-op does not dirty/persist or enter history.
  REQUIRE(!f.undo.perform(std::make_unique<fiddle::EditChairsAction>(repository, f.mixer,
      std::vector<Edit>{{section.id, "Should not apply", std::nullopt},
                        {"absent", std::nullopt, fiddle::DoricoRole::solo}}, changed)));
  REQUIRE(failures == 1 && repository.getChair(section.id)->name == "Renamed");
  REQUIRE(repository.getChair(solo.id)->flatIndex == solo.flatIndex);
}

void testChairDeletionRetainsLayers() {
  Sandbox sandbox;
  fiddle::FiddleDatabase db;
  REQUIRE(db.open(sandbox.directory.getChildFile("chair-delete.sqlite")));
  auto &repository = *db.getLibraryRoutingRepository();
  MixerFixture f;
  fiddle::ChairRow chair;
  chair.id = "violin"; chair.name = "Violin"; chair.instrumentEntityId = "test.violin";
  REQUIRE(repository.insertChairWithStableAssignment(chair));
  std::vector<juce::String> ids;
  std::vector<fiddle::MixerStrip *> originals;
  for (int i = 0; i < 3; ++i) {
    const auto id = f.instrument(0.1f, 1);
    ids.push_back(id);
    originals.push_back(f.mixer.getStrip(id));
    if (i == 1) continue; // An unrelated strip interleaved in mixer order.
    auto *strip = originals.back();
    strip->chairId = chair.id;
    strip->setGainDb(-6);
    fiddle::LayerRow layer;
    layer.id = id.toStdString(); layer.chairId = chair.id;
    layer.position = i; layer.patchId = "missing-catalog"; layer.patchName = "Historical violin";
    REQUIRE(repository.upsertLayer(layer));
    addEffect(strip->audioEngine(), "fx", 2.0f);
  }
  REQUIRE(f.commands.addGroupBus("Strings", {ids[0], ids[2]}));
  const auto busId = originals[0]->directOutputBusId;
  auto settings = f.mixer.projectSettings();
  settings.lockedChairIds.insert(chair.id); settings.playbackDelayMs = 850;
  f.mixer.setProjectSettings(settings);
  f.undo.clear();
  auto changed = [&](bool success) { REQUIRE(success); };
  f.noteOn(1);
  const auto preparations = SignalProcessor::preparations.size();
  REQUIRE(f.undo.perform(std::make_unique<fiddle::RemoveChairAction>(repository, f.mixer, chair.id, changed)));
  for (int cycle = 0; cycle < 3; ++cycle) {
    REQUIRE(!repository.getChair(chair.id) && repository.listLayers(chair.id).empty());
    REQUIRE(f.mixer.size() == 1 && f.mixer.getStrip(ids[1]) == originals[1]);
    REQUIRE(!f.mixer.projectSettings().lockedChairIds.count(chair.id));
    f.expectLevel(0.1f);
    REQUIRE(f.undo.undo() && f.undo.isAtSavePoint());
    REQUIRE(repository.getChair(chair.id)->flatIndex == chair.flatIndex);
    REQUIRE(repository.listLayers(chair.id).size() == 2);
    REQUIRE(f.mixer.projectSettings() == settings);
    for (int i : {0, 2}) {
      REQUIRE(f.mixer.getStrip(ids[i]) == originals[i]);
      REQUIRE(f.mixer.stripIndex(ids[i]) == i);
      REQUIRE(originals[i]->directOutputBusId == busId && originals[i]->gainDb() == -6);
      REQUIRE(originals[i]->audioEngine().snapshot("fx"));
    }
    REQUIRE(SignalProcessor::preparations.size() == preparations);
    f.expectLevel(0.1f); // Undo does not resurrect held or future notes.
    f.noteOn(1);
    f.expectLevel(0.1f + 0.4f * juce::Decibels::decibelsToGain(-6.0f));
    if (cycle != 2) REQUIRE(f.undo.redo());
  }
  // The next matching chair uses a new MIDI assignment, not the restored one.
  auto next = chair; next.id = "new-violin";
  REQUIRE(repository.insertChairWithStableAssignment(next));
  REQUIRE(next.flatIndex != chair.flatIndex);
}

void testChairLayerRemovalUndo() {
  Sandbox sandbox;
  const auto file = sandbox.directory.getChildFile("layer-undo.sqlite");
  juce::String removedId;
  {
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(file));
    auto &repository = *db.getLibraryRoutingRepository();
    MixerFixture f;
    fiddle::ChairRow chair;
    chair.id = "violin-1";
    chair.name = "Violin 1";
    chair.instrumentEntityId = "test.violin";
    chair.family = "strings";
    chair.role = fiddle::DoricoRole::section;
    chair.flatIndex = 1;
    REQUIRE(repository.upsertChair(chair));
    std::vector<juce::String> ids;
    for (int i = 0; i < 3; ++i) {
      const auto id = f.instrument(0.2f, 1);
      ids.push_back(id);
      fiddle::LayerRow layer;
      layer.id = id.toStdString();
      layer.chairId = chair.id;
      layer.patchId = "historical-patch-" + std::to_string(i);
      layer.patchName = "Violin layer " + std::to_string(i);
      layer.libraryName = "Test strings";
      layer.position = i;
      layer.sourcePatchRevision = 7;
      layer.pluginStateEdited = true;
      REQUIRE(repository.upsertLayer(layer));
      auto *strip = f.mixer.getStrip(id);
      strip->chairId = chair.id;
      strip->patchId = layer.patchId;
      strip->layerName = layer.patchName;
      strip->library = layer.libraryName;
    }
    removedId = ids[1];
    auto *original = f.mixer.getStrip(removedId);
    const float gain = juce::Decibels::gainToDecibels(0.5f);
    original->setGainDb(gain); // Live edit newer than the database row.
    addEffect(original->audioEngine(), "retained-fx", 2.0f);
    REQUIRE(f.commands.addGroupBus("Strings", {removedId}));
    const auto busId = original->directOutputBusId;
    f.undo.clear();
    f.undo.markSavePoint();
    int notifications = 0;
    auto changed = [&](bool success) {
      REQUIRE(success);
      ++notifications;
      // Same persistence contract as the UI callback: the restored instance
      // is authoritative, not the stale catalog/database defaults.
      db.clearStrips();
      int index = 0;
      for (auto *strip : f.mixer.getAllStrips()) {
        db.saveStrip(*strip, index++);
        db.saveStripAudio(strip->id, strip->audioEngine().snapshotAll());
        auto row = repository.getLayer(strip->id.toStdString());
        REQUIRE(row);
        row->gainDb = strip->gainDb();
        REQUIRE(repository.upsertLayer(*row));
      }
    };
    const auto preparationCount = SignalProcessor::preparations.size();
    f.noteOn(1);
    f.expectLevel(0.6f);
    f.undo.perform(std::make_unique<fiddle::RemoveChairLayerAction>(
        repository, f.mixer, *repository.getLayer(removedId.toStdString()), changed));
    for (int cycle = 0; cycle < 3; ++cycle) {
      REQUIRE(!repository.getLayer(removedId.toStdString()));
      REQUIRE(!f.mixer.getStrip(removedId));
      REQUIRE(repository.listLayers(chair.id).size() == 2);
      REQUIRE(db.loadAllStrips().size() == 2);
      REQUIRE(!f.undo.isAtSavePoint());
      f.expectLevel(0.4f);
      REQUIRE(f.undo.undo());
      REQUIRE(f.undo.isAtSavePoint());
      REQUIRE(f.mixer.getStrip(removedId) == original);
      REQUIRE(f.mixer.stripIndex(removedId) == 1);
      REQUIRE(original->hasPlugin());
      REQUIRE(original->gainDb() == gain);
      REQUIRE(original->directOutputBusId == busId);
      REQUIRE(original->audioEngine().snapshotAll().preFaderInserts.size() == 1);
      const auto rows = repository.listLayers(chair.id);
      REQUIRE(rows.size() == 3 && rows[1].id == removedId.toStdString());
      REQUIRE(rows[1].sourcePatchRevision == 7 && rows[1].pluginStateEdited);
      REQUIRE(rows[1].gainDb == gain);
      REQUIRE(SignalProcessor::preparations.size() == preparationCount);
      f.expectLevel(0.4f); // No resurrected held note on the restored player.
      f.noteOn(1);
      f.expectLevel(0.6f); // It plays again, including its retained FX/gain.
      if (cycle < 2)
        REQUIRE(f.undo.redo());
    }
    REQUIRE(notifications == 6);
    REQUIRE(db.loadAllStrips().size() == 3);
  }
  fiddle::FiddleDatabase reopened;
  REQUIRE(reopened.open(file));
  const auto layer = reopened.getLibraryRoutingRepository()->getLayer(removedId.toStdString());
  REQUIRE(layer && layer->position == 1 && layer->sourcePatchRevision == 7);
  REQUIRE(reopened.loadAllStrips().size() == 3);
}

void testChairLayerAdditionUndo() {
  // Exercise ordinary loading, Undo before async creation finishes, and an
  // unavailable player. None may cause Redo to make a fresh catalog copy.
  for (int mode = 0; mode < 4; ++mode) {
    Sandbox sandbox;
    fiddle::FiddleDatabase db;
    REQUIRE(db.open(sandbox.directory.getChildFile("layer-add.sqlite")));
    REQUIRE(db.saveLibraryMetadata({"library", "Test strings", "Test", ""}));
    auto &repository = *db.getLibraryRoutingRepository();
    fiddle::ChairRow chair;
    chair.id = "violin-1";
    chair.name = "Violin 1";
    chair.instrumentEntityId = "test.violin";
    chair.flatIndex = 1;
    REQUIRE(repository.upsertChair(chair));
    fiddle::LibraryPatchRow patch;
    patch.id = "violin-patch";
    patch.libraryId = "library";
    patch.name = "Original violin";
    patch.pluginUid = description(true).uniqueId;
    const float amount = 0.2f;
    patch.pluginState.resize(sizeof(amount));
    std::memcpy(patch.pluginState.data(), &amount, sizeof(amount));
    REQUIRE(repository.upsertPatch(patch));
    MixerFixture f;
    auto ownedFormat = std::make_unique<TestPluginFormat>();
    auto *format = ownedFormat.get();
    format->defer = mode == 1 || mode == 3;
    format->missingInstruments = mode == 2;
    f.mixer.getFormatManager().addFormat(std::move(ownedFormat));
    const auto existingId = f.instrument(0.1f, 2);
    f.mixer.getStrip(existingId)->setGainDb(-7.0f);
    REQUIRE(f.commands.addGroupBus("Strings", {}));
    const auto busId = f.mixer.getAllGroupBuses().front()->id;
    f.undo.clear();
    f.undo.markSavePoint();
    int creations = 0, notifications = 0;
    bool completed = false;
    juce::String id;
    fiddle::MixerStrip *original = nullptr;
    auto instantiate = [&](const fiddle::LayerRow &layer) {
      ++creations;
      id = layer.id;
      auto strip = std::make_shared<fiddle::MixerStrip>();
      original = strip.get();
      strip->id = id;
      strip->chairId = layer.chairId;
      strip->patchId = layer.patchId;
      strip->layerName = layer.patchName;
      strip->library = layer.libraryName;
      strip->setInputAssignment(0, 1);
      f.mixer.insertStripAt(strip, f.mixer.size());
      juce::MemoryBlock initialState(layer.pluginState.data(), layer.pluginState.size());
      strip->loadPlugin(description(true), f.mixer.getFormatManager(),
                        [&](bool success) {
                          REQUIRE(success == (mode != 2));
                          completed = true;
                        }, initialState);
      return true;
    };
    auto changed = [&](bool success) {
      REQUIRE(success);
      ++notifications;
      db.clearStrips();
      int index = 0;
      for (auto *strip : f.mixer.getAllStrips()) {
        db.saveStrip(*strip, index++);
        db.saveStripAudio(strip->id, strip->audioEngine().snapshotAll());
        if (auto layer = repository.getLayer(strip->id.toStdString())) {
          layer->gainDb = strip->gainDb();
          REQUIRE(repository.upsertLayer(*layer));
        }
      }
    };
    f.undo.perform(std::make_unique<fiddle::AddChairLayerAction>(
        repository, f.mixer, chair.id, patch.id, 0, instantiate, changed));
    REQUIRE(f.undo.canUndo() && !f.undo.isAtSavePoint());
    REQUIRE(repository.listLayers(chair.id).size() == 1);
    if (mode == 1 || mode == 3) {
      pumpUntil([&] { return !format->pending.empty(); });
      REQUIRE(!completed);
      if (mode == 3)
        REQUIRE(f.undo.perform(std::make_unique<fiddle::RemoveChairAction>(repository, f.mixer, chair.id, changed)));
      else
        REQUIRE(f.undo.undo());
      REQUIRE(!f.mixer.getStrip(id));
      format->completeAll(); // Apply saved state while retained only by undo.
      pumpUntil([&] { return completed; });
      if (mode == 3)
        REQUIRE(f.undo.undo()); // Restore the whole chair after its load completed off-mixer.
      else
        REQUIRE(f.undo.redo());
    } else {
      pumpUntil([&] { return completed; });
    }
    REQUIRE(original->cachedPluginState().getSize() == sizeof(amount));
    float restored = 0;
    std::memcpy(&restored, original->cachedPluginState().getData(), sizeof(restored));
    REQUIRE(restored == amount);
    const float gain = juce::Decibels::gainToDecibels(0.5f);
    original->setGainDb(gain);
    REQUIRE(f.mixer.setStripDirectOutput(id, busId));
    addEffect(original->audioEngine(), "added-layer-fx", 2.0f);
    const auto preparations = SignalProcessor::preparations.size();
    for (int cycle = 0; cycle < 3; ++cycle) {
      REQUIRE(f.undo.undo());
      REQUIRE(f.undo.isAtSavePoint());
      REQUIRE(!f.mixer.getStrip(id));
      REQUIRE(!repository.getLayer(id.toStdString()));
      REQUIRE(db.loadAllStrips().size() == 1);
      patch.name = "Changed catalog patch";
      patch.pluginState.assign(sizeof(float), 0);
      REQUIRE(repository.upsertPatch(patch));
      REQUIRE(f.undo.redo());
      REQUIRE(!f.undo.isAtSavePoint());
      REQUIRE(creations == 1);
      REQUIRE(f.mixer.getStrip(id) == original);
      REQUIRE(f.mixer.stripIndex(id) == 1);
      REQUIRE(original->layerName == "Original violin");
      REQUIRE(original->gainDb() == gain);
      REQUIRE(original->directOutputBusId == busId);
      REQUIRE(original->audioEngine().snapshotAll().preFaderInserts.size() == 1);
      REQUIRE(repository.getLayer(id.toStdString())->patchName == "Original violin");
      REQUIRE(repository.getLayer(id.toStdString())->gainDb == gain);
      REQUIRE(db.loadAllStrips().size() == 2);
      REQUIRE(f.mixer.getStrip(existingId)->gainDb() == -7.0f);
      REQUIRE(SignalProcessor::preparations.size() == preparations);
      f.expectLevel(0.0f); // Clear queued panic before a new note.
      f.noteOn(1);
      f.expectLevel(mode == 2 ? 0.0f : 0.2f);
    }
    REQUIRE(notifications == (mode == 1 || mode == 3 ? 9 : 7));
    // Removing an added layer and undoing both actions must also compose.
    f.undo.perform(std::make_unique<fiddle::RemoveChairLayerAction>(
        repository, f.mixer, *repository.getLayer(id.toStdString()), changed));
    REQUIRE(f.undo.undo());
    REQUIRE(f.mixer.getStrip(id) == original);
    REQUIRE(f.undo.undo());
    REQUIRE(!f.mixer.getStrip(id));
    REQUIRE(f.undo.isAtSavePoint());
    REQUIRE(f.undo.redo());
    REQUIRE(f.mixer.getStrip(id) == original);
    REQUIRE(f.undo.redo());
    REQUIRE(!f.mixer.getStrip(id));
  }
}

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
    f.mixer.setProjectSettings({725, {"chair-a", "chair-b"}});

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
    const fiddle::ProjectSettings settings{725, {"chair-a", "chair-b"}};
    REQUIRE(state->globalState.projectSettings == settings);
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
    REQUIRE(restored->projectSettings == settings);
    REQUIRE(!restored->ancestorHashes.empty());
    REQUIRE(restored->ancestorHashes.back() == savedVersion);
    REQUIRE(restored->strips.size() == 2 && restored->strips.front().muted);
  }
}

void testProjectSettingsAndAtomicMixerUndo() {
  MixerFixture f;
  const auto a = f.instrument(0.1f, 1), b = f.instrument(0.2f, 2);
  const fiddle::ProjectSettings initial = f.mixer.projectSettings();
  const fiddle::ProjectSettings edited{650, {"chair-a"}};
  REQUIRE(f.undo.perform(std::make_unique<fiddle::SetProjectSettingsAction>(f.mixer, edited, "Project settings")));
  REQUIRE(f.mixer.projectSettings() == edited);
  REQUIRE(f.undo.undo() && f.mixer.projectSettings() == initial);
  REQUIRE(f.undo.redo() && f.mixer.projectSettings() == edited);

  using Action = fiddle::SetMixerControlsAction;
  const auto beforeA = Action::Values::of(*f.mixer.getStrip(a));
  const auto beforeB = Action::Values::of(*f.mixer.getStrip(b));
  auto afterA = beforeA, afterB = beforeB;
  afterA.muted = true;
  afterB.gainDb = 3;
  f.undo.clear();
  REQUIRE(f.undo.perform(std::make_unique<Action>(f.mixer,
      std::vector<Action::Change>{{a, beforeA, afterA}, {b, beforeB, afterB}}, "Mute layer")));
  REQUIRE(f.mixer.getStrip(a)->isMuted() && f.mixer.getStrip(b)->gainDb() == 3);
  REQUIRE(f.undo.undo());
  REQUIRE(Action::Values::of(*f.mixer.getStrip(a)) == beforeA);
  REQUIRE(Action::Values::of(*f.mixer.getStrip(b)) == beforeB);
  REQUIRE(!f.undo.canUndo());
  REQUIRE(f.undo.redo());

  // A missing target rejects the complete edit, preserving model and history.
  REQUIRE(!f.undo.perform(std::make_unique<Action>(f.mixer,
      std::vector<Action::Change>{{a, afterA, beforeA}, {"missing", beforeB, afterB}}, "Invalid batch")));
  REQUIRE(f.mixer.getStrip(a)->isMuted());
  REQUIRE(f.undo.undo());

  // Identically named gain edits for different target sets must not merge.
  f.undo.clear();
  afterA = beforeA; afterA.gainDb = -2;
  afterB = beforeB; afterB.gainDb = -4;
  f.undo.perform(std::make_unique<Action>(f.mixer,
      std::vector<Action::Change>{{a, beforeA, afterA}}, "Adjust layer gains", true));
  f.undo.perform(std::make_unique<Action>(f.mixer,
      std::vector<Action::Change>{{b, beforeB, afterB}}, "Adjust layer gains", true));
  REQUIRE(f.undo.undo() && f.mixer.getStrip(b)->gainDb() == beforeB.gainDb);
  REQUIRE(f.mixer.getStrip(a)->gainDb() == -2);
  REQUIRE(f.undo.undo() && f.mixer.getStrip(a)->gainDb() == beforeA.gainDb);

  // A compound command rolls back earlier children if a later child rejects.
  std::vector<std::unique_ptr<fiddle::UndoableAction>> children;
  children.push_back(std::make_unique<Action>(f.mixer,
      std::vector<Action::Change>{{a, beforeA, afterA}}, "First child"));
  children.push_back(std::make_unique<Action>(f.mixer,
      std::vector<Action::Change>{{"missing", beforeB, afterB}}, "Rejected child"));
  REQUIRE(!f.undo.perform(std::make_unique<fiddle::CompoundAction>("Rejected compound", std::move(children))));
  REQUIRE(Action::Values::of(*f.mixer.getStrip(a)) == beforeA);
  REQUIRE(!f.undo.canUndo() && f.undo.canRedo());
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
    f.mixer.setProjectSettings({825, {"historical-chair"}});
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
    const fiddle::ProjectSettings expectedSettings{825, {"historical-chair"}};
    REQUIRE(f.mixer.projectSettings() == expectedSettings);
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

void testVariableBlockContinuity() {
  for (const bool grouped : {false, true}) {
    MixerFixture f;
    const auto id = f.mixer.addStrip();
    auto *strip = f.mixer.getStrip(id);
    auto instrument = std::make_unique<SignalProcessor>(true, 0.00001f);
    instrument->counting = true;
    auto *counter = instrument.get();
    juce::String error;
    REQUIRE(strip->installInstrumentProcessor(description(true), std::move(instrument), error));
    addEffect(strip->audioEngine(), "delay-strip", 1.0f, 5);
    if (grouped) {
      REQUIRE(f.commands.addGroupBus("Group", {id}));
      addEffect(f.mixer.getAllGroupBuses().front()->audioEngine(), "delay-bus", 1.0f, 7);
    }
    fiddle::MasterInsertSnapshot master;
    master.slotId = "delay-master"; master.description = description(false);
    REQUIRE(f.mixer.masterAudio().insertProcessor(master, 0, std::make_unique<SignalProcessor>(false, 1.0f, 3)));
    const int delay = grouped ? 15 : 8;
    int frames = 0;
    for (int cycle = 0; cycle < 30; ++cycle) {
      for (const int size : {0, 1, 16, 3, 64, 7, 32, 65}) {
        juce::AudioBuffer<float> audio(2, size); audio.clear();
        f.mixer.processBlock(audio, f.now + 1000.0 * frames / f.sampleRate);
        if (size > 64) {
          REQUIRE(audio.getMagnitude(0, size) == 0);
        } else {
          for (int i = 0; i < size; ++i) {
            const auto expected = frames + i < delay ? 0.0f : 0.00001f * (frames + i - delay + 1);
            REQUIRE(std::abs(audio.getSample(0, i) - expected) < 0.000001f);
            REQUIRE(audio.getSample(0, i) == audio.getSample(1, i));
          }
          frames += size;
        }
        REQUIRE(counter->frames == static_cast<uint64_t>(frames));
      }
    }
  }
}

void testVariableBlockMidiTiming() {
  for (const bool grouped : {false, true}) {
    MixerFixture f;
    const auto id = f.instrument(0.1f, 1);
    if (grouped) REQUIRE(f.commands.addGroupBus("Group", {id}));
    f.now = 1000;
    f.noteOn(1, 7.0 * 1000 / f.sampleRate);
    f.mixer.routeNoteEvent(0, 1, juce::MidiMessage::noteOff(1, 60),
                          f.now + 80.0 * 1000 / f.sampleRate);
    int frame = 0, first = -1, last = -1;
    for (const auto count : {3, 0, 16, 1, 64, 7, 32}) {
      juce::AudioBuffer<float> out(2, count); out.clear();
      f.mixer.processBlock(out, f.now + frame * 1000.0 / f.sampleRate);
      for (int i = 0; i < count; ++i) if (out.getSample(0, i) != 0) {
        if (first < 0) first = frame + i;
        last = frame + i;
      }
      frame += count;
    }
    REQUIRE(std::abs(first - 7) <= 1 && std::abs(last - 79) <= 1);
  }
}

void testUndoRetainsInFlightGraph() {
  MixerFixture f;
  const auto id = f.mixer.addStrip();
  auto instrument = std::make_unique<SignalProcessor>(true, 0.1f);
  std::atomic<bool> entered{false}, release{false}, destroyed{false};
  instrument->onProcess = [&] { entered.store(true); while (!release.load()) std::this_thread::yield(); };
  instrument->onDestroy = [&] { destroyed.store(true); };
  juce::String error;
  REQUIRE(f.mixer.getStrip(id)->installInstrumentProcessor(description(true), std::move(instrument), error));
  REQUIRE(f.commands.addGroupBus("Group", {id}));
  const auto busId = f.mixer.getAllGroupBuses().front()->id;
  std::thread audio([&] { f.render(); });
  while (!entered.load()) std::this_thread::yield();
  const auto preparations = SignalProcessor::preparations.size();
  auto strip = f.mixer.removeStripKeepAlive(id);
  auto bus = f.mixer.removeGroupBusKeepAlive(busId);
  f.mixer.insertGroupBusAt(std::move(bus), 0);
  f.mixer.insertStripAt(std::move(strip), 0);
  const bool unchanged = SignalProcessor::preparations.size() == preparations;
  f.undo.perform(std::make_unique<fiddle::RemoveStripAction>(f.mixer, id));
  f.mixer.removeGroupBus(busId);
  f.undo.clear(); // used to delete the strip while the render was inside it
  static_cast<juce::Timer &>(f.mixer).timerCallback();
  const bool retained = !destroyed.load();
  release.store(true); audio.join();
  REQUIRE(unchanged && retained);
  static_cast<juce::Timer &>(f.mixer).timerCallback();
  REQUIRE(destroyed.load());
}

void testConcurrentMidiInspectorChanges() {
  MixerFixture f;
  const auto id = f.instrument(0.1f, 0);
  auto *strip = f.mixer.getStrip(id);
  std::atomic<bool> finished{false};
  std::thread midi([&] {
    for (int i = 0; i < 1000; ++i) {
      fiddle::Note note; note.set_note_number(60); note.set_channel(1); note.set_start_velocity(80);
      f.mixer.routeAnnotatedNoteOn(0, 0, note, 0);
      f.mixer.routeAnnotatedNoteOff(0, 0, note, 0);
      f.mixer.recordIncomingMidi(0, 0, fiddle::CapturedMidiEvent::NoteOn, 60, 80, f.now);
    }
    finished.store(true);
  });
  int snapshots = 0;
  do {
    strip->setExpressionMap(std::make_shared<fiddle::ExpressionMapData>());
    strip->clearAnnotations();
    strip->getAnnotationRecordsAsJson();
    strip->setExpressionMap(nullptr);
    f.mixer.allNotesOff();
    ++snapshots;
  } while (!finished.load());
  midi.join();
  REQUIRE(snapshots > 0);
}

void testRenderAheadWorkerAndRemapping() {
  Sandbox sandbox;
  MixerFixture f;
  f.instrument(0.1f, 1);
  fiddle::AudioRenderDiagnostics recorder;
  const auto file = sandbox.directory.getChildFile("audio-v2.mmap");
  fiddle::RenderAheadEngine worker(f.mixer, recorder, file);
  REQUIRE(worker.start(48000, 512).isEmpty());
  const auto firstId = worker.streamId();
  fiddle::AudioConsumer consumer(file.getFullPathName().toStdString());
  juce::MemoryMappedFile mapping(file, juce::MemoryMappedFile::readWrite);
  auto &ring = *static_cast<fiddle::AudioStreamRing *>(mapping.getData());
  const auto base = fiddle::audioStreamTimeMs();
  // A one-second note deadline is unchanged by a 64-ms audio reserve.
  f.now = base;
  f.noteOn(1, 1000);
  f.mixer.routeNoteEvent(0, 1, juce::MidiMessage::noteOff(1, 60), base + 1200);
  juce::AudioBuffer<float> out(2, 512);
  int firstSound = -1, lastSound = -1;
  for (int frame = 0; frame < 48000 * 3 / 2; frame += 512) {
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000;
    while (ring.writeFrame.load() < ring.readFrame.load() + 512 &&
           juce::Time::getMillisecondCounterHiRes() < deadline)
      juce::Thread::sleep(1);
    REQUIRE(ring.writeFrame.load() >= ring.readFrame.load() + 512);
    // Virtual host time makes timing exact, independent of test-machine speed;
    // the actual production worker still runs and publishes through the mmap.
    REQUIRE(ring.pull(out.getArrayOfWritePointers(), 2, 512, 48000,
                      base + frame * 1000.0 / 48000) == fiddle::AudioStreamRing::PullResult::audio);
    for (int i = 0; i < 512; ++i) {
      if (out.getSample(0, i) != 0) {
        if (firstSound < 0) firstSound = frame + i;
        lastSound = frame + i;
        REQUIRE(std::abs(out.getSample(0, i) - 0.1f) < 0.00001f);
      }
    }
  }
  REQUIRE(std::abs(firstSound - 48000) <= 1);
  REQUIRE(std::abs(lastSound - 57599) <= 1);
  {
    fiddle::AudioProcessingGate::Control control;
    const auto written = ring.writeFrame.load();
    // Consume queued audio during state work. The worker must not publish a
    // fabricated silent block or advance DSP while its control gate is closed.
    (void)ring.pull(out.getArrayOfWritePointers(), 2, 512, 48000,
                    base + 1500);
    juce::Thread::sleep(5);
    REQUIRE(ring.writeFrame.load() == written);
  }
  worker.stop();
  REQUIRE(ring.active.load() == 0);
  REQUIRE(worker.start(44100, 1024).isEmpty());
  REQUIRE(worker.streamId() != firstId);
  // Old inode stays valid but inactive; no cursor reset under an old reader.
  REQUIRE(ring.active.load() == 0 && ring.sampleRate == 48000);
  consumer.pullAudio(out.getArrayOfWritePointers(), 2, 512, 44100, worker.streamId());
  REQUIRE(consumer.diagnostics().unavailableFrames == 512);
  consumer.remap();
  consumer.pullAudio(out.getArrayOfWritePointers(), 2, 512, 44100, worker.streamId());
  REQUIRE(consumer.diagnostics().unavailableFrames == 512);
  REQUIRE(out.getMagnitude(0, 512) == 0); // freshly primed generation
  worker.stop();
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
  run("expression-map imports undo and restore without source files", testExpressionMapUndoAndImportedPersistence);
  run("separate catalog history retains data and rejected redo", testLibraryCatalogHistory);
  run("library preview state survives structural history and duplication", testRetainedLibraryPreviews);
  run("meter snapshots avoid plugin queries and state capture", testMeterOnlySnapshots);
  run("plugin timings and buffer changes cover instruments and every FX path", testPluginTimingAndReprepare);
  run("save freezes each plugin state once for session and version persistence", testFrozenSaveCapturesEachPluginOnce);
  run("UI bus removal and undo/redo", testBusRemovalThroughUiCommandsAndUndo);
  run("instrument replacement undo preserves both edited states", testInstrumentReplacementStateUndo);
  run("batch library refresh preserves independent live, imported, pending and missing state", testLayerLibrarySetupUndo);
  run("FX add/remove cycles preserve edited and pending state on all racks", testFxStateUndo);
  run("chair create and atomic metadata edit undo", testChairCommandsUndo);
  run("chair deletion retains live layers, locks, FX and assignments", testChairDeletionRetainsLayers);
  run("chair layer removal restores assignment, live player and FX on undo", testChairLayerRemovalUndo);
  run("chair layer addition undo/redo retains original and asynchronously loaded players", testChairLayerAdditionUndo);
  run("sample alignment through strip and bus latency", testRealSampleLatencyAcrossDirectAndBusRoutes);
  run("panic clears sounding and future notes", testPanicClearsSoundingAndFutureNotes);
  run("graceful stop tolerates incomplete note tracking", testGracefulStopWithIncompleteNoteTracking);
  run("saved routing survives database reopen", testSnapshotSurvivesDatabaseReopenAndStateExchange);
  run("project settings and atomic compensated mixer undo", testProjectSettingsAndAtomicMixerUndo);
  run("historical mixer save forks and survives reopen", testHistoricalMixerSaveForkSurvivesReopen);
  run("restore recreates audio after database reopen", testRestoreRecreatesAudioAfterDatabaseReopen);
  run("missing plug-ins and superseded restore", testMissingPluginsAndSupersededRestore);
  run("cancelled rebuild retains published host state", testCancelledStateRebuildRetainsPublishedBlob);
  run("render-ahead worker preserves note deadlines and remaps generations", testRenderAheadWorkerAndRemapping);
  run("variable block lengths preserve instrument and FX samples", testVariableBlockContinuity);
  run("variable block lengths preserve MIDI note deadlines", testVariableBlockMidiTiming);
  run("undo disposal retains in-flight graphs without re-preparing", testUndoRetainsInFlightGraph);
  run("concurrent MIDI, expression-map edits and inspector snapshots", testConcurrentMidiInspectorChanges);
  return failed == 0 ? 0 : 1;
}
