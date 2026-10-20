#include "HostMidiBlock.h"
#include "MidiProbeCapture.h"
#include <iostream>
#include <memory>
#include <vector>
#include <limits>
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace fiddle;
static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::cerr << __LINE__ << ": " << #x << '\n'; } } while (false)
#define UNKNOWN_METHODS \
  tresult PLUGIN_API queryInterface(const TUID, void **object) override { *object = nullptr; return kNoInterface; } \
  uint32 PLUGIN_API addRef() override { return 1; } \
  uint32 PLUGIN_API release() override { return 1; }
struct Queue : IParamValueQueue {
  ParamID id = 0;
  std::vector<std::pair<int32, double>> points;
  UNKNOWN_METHODS
  ParamID PLUGIN_API getParameterId() override { return id; }
  int32 PLUGIN_API getPointCount() override { return static_cast<int32>(points.size()); }
  tresult PLUGIN_API getPoint(int32 i, int32 &offset, ParamValue &value) override {
    if (i < 0 || i >= getPointCount()) return kResultFalse;
    offset = points[i].first; value = points[i].second; return kResultOk;
  }
  tresult PLUGIN_API addPoint(int32 offset, ParamValue value, int32 &i) override {
    i = getPointCount(); points.emplace_back(offset, value); return kResultOk;
  }
};
struct Parameters : IParameterChanges {
  std::vector<Queue *> queues;
  UNKNOWN_METHODS
  int32 PLUGIN_API getParameterCount() override { return static_cast<int32>(queues.size()); }
  IParamValueQueue *PLUGIN_API getParameterData(int32 i) override { return queues.at(i); }
  IParamValueQueue *PLUGIN_API addParameterData(const ParamID &, int32 &) override { return nullptr; }
};
struct Events : IEventList {
  std::vector<Event> events;
  UNKNOWN_METHODS
  int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events.size()); }
  tresult PLUGIN_API getEvent(int32 i, Event &event) override {
    if (i < 0 || i >= getEventCount()) return kResultFalse;
    event = events[i]; return kResultOk;
  }
  tresult PLUGIN_API addEvent(Event &event) override { events.push_back(event); return kResultOk; }
};
static Event note(int offset, int pitch, int velocity, int bus = 0, int channel = 0) {
  Event e{}; e.type = Event::kNoteOnEvent; e.sampleOffset = offset; e.busIndex = bus;
  e.noteOn.channel = channel; e.noteOn.pitch = pitch; e.noteOn.velocity = velocity / 127.0f;
  return e;
}
int main() {
  auto block = std::make_unique<HostMidiBlock>();
  Queue cc; cc.id = FiddleController::kCCParamBase + 3 * 256; // CC102
  cc.points = {{0, 0.0}, {12, 2.0 / 127}, {48, 7.0 / 127}};
  Queue program; program.id = 100 + 17; program.points = {{20, 40.0 / 127}, {30, 41.0 / 127}};
  Parameters params; params.queues = {&cc, &program};
  Events events; events.events = {note(8, 72, 90), note(12, 0, 1), note(12, 127, 126), note(24, 60, 64, 1, 1)};
  ProcessContext context{}; context.projectTimeSamples = 9000; // No music-valid flag!
  ProcessData data{}; data.numSamples = 64; data.processContext = &context;
  data.inputParameterChanges = &params; data.inputEvents = &events;
  block->collect(data);
  CHECK(block->count == 9); CHECK(block->invalid == 0); CHECK(block->dropped == 0);
  const int offsets[] = {0, 8, 12, 12, 12, 20, 24, 30, 48};
  for (size_t i = 0; i < block->count; ++i) {
    CHECK(block->inputs[i].midi.timestampSamples == offsets[i]);
    CHECK(block->inputs[i].midi.hostSamplePosition == uint64_t(9000 + offsets[i]));
  }
  CHECK(block->inputs[2].source == 0); CHECK(block->inputs[2].pointIndex == 1);
  CHECK(block->inputs[3].midi.data1 == 0); CHECK(block->inputs[4].midi.data1 == 127);
  CHECK(block->inputs[5].midi.port == 1 && block->inputs[5].midi.channel == 2);
  CHECK(block->inputs[5].midi.data1 == 40); CHECK(block->inputs[7].midi.data1 == 41);
  CHECK(block->inputs[6].midi.port == 1 && block->inputs[6].midi.channel == 2);
  for (int v = 0; v < 128; ++v) {
    CHECK(HostMidiBlock::midi7(v / 127.0f) == v);
    if (v > 0 && v < 127) CHECK(HostMidiBlock::midi7(std::nextafter(float(v / 127.0f), 0.0f)) == v);
  }
  CHECK(HostMidiBlock::midi7(std::numeric_limits<double>::quiet_NaN()) == -1);
  CHECK(HostMidiBlock::midi7(-0.1) == -1); CHECK(HostMidiBlock::midi7(1.1) == -1);
  events.events = {note(-1, 72, 90), note(64, 72, 90), note(0, 72, 90, 16), note(0, 72, 90, 0, 16)};
  data.inputParameterChanges = nullptr; block->collect(data);
  CHECK(block->invalid == 4);
  events.events = {note(0, 0, 0), note(0, 127, 127)}; data.numSamples = 0;
  context.projectTimeSamples = -20; block->collect(data);
  CHECK(block->invalid == 0); CHECK(block->inputs[0].midi.data2 == 0);
  CHECK(block->inputs[1].midi.data2 == 127); CHECK(block->inputs[0].midi.hostSamplePosition == 0);
  events.events.assign(HostMidiBlock::capacity + 7, note(0, 72, 90)); block->collect(data);
  CHECK(block->count == HostMidiBlock::capacity); CHECK(block->dropped == 7);
  events.events.clear(); cc.points.assign(HostMidiBlock::capacity + 5, {0, 0.5});
  data.inputParameterChanges = &params; block->collect(data);
  CHECK(block->count == HostMidiBlock::capacity); CHECK(block->dropped == 7);
  data.inputParameterChanges = nullptr; data.processContext = nullptr; block->collect(data);
  CHECK(block->count == 0); CHECK(block->dropped == 0);
  {
    // Added controllers: pedals, channel pressure and 14-bit pitch bend.
    const auto param = [](int controller, int logical) {
      for (int i = 0; i < FiddleController::kNumSupportedCCs; ++i)
        if (FiddleController::kSupportedCCs[i] == controller)
          return ParamID(FiddleController::kCCParamBase + i * FiddleController::kNumChannels + logical);
      return ParamID(0);
    };
    CHECK(FiddleController::kNumSupportedCCs == 122);
    CHECK(FiddleController::ccToIndex(102) == 3); // legacy IDs unchanged
    CHECK(FiddleController::ccToIndex(123) == -1 && FiddleController::ccToIndex(120) == -1);
    Queue sustain; sustain.id = param(64, 18); sustain.points = {{4, 127.0 / 127}};
    Queue pressure; pressure.id = param(kAfterTouch, 18); pressure.points = {{5, 64.0 / 127}};
    Queue bend; bend.id = param(kPitchBend, 18); bend.points = {{6, 0.5}, {7, 1.0}};
    Parameters controllers; controllers.queues = {&sustain, &pressure, &bend};
    ProcessContext ctx{}; ProcessData d{}; d.numSamples = 64; d.processContext = &ctx;
    d.inputParameterChanges = &controllers; block->collect(d);
    CHECK(block->count == 4 && block->invalid == 0);
    CHECK(block->inputs[0].midi.type == RealtimeMidiEventType::ControlChange);
    CHECK(block->inputs[0].midi.data1 == 64 && block->inputs[0].midi.data2 == 127);
    CHECK(block->inputs[0].midi.port == 1 && block->inputs[0].midi.channel == 3);
    CHECK(block->inputs[1].midi.type == RealtimeMidiEventType::ChannelPressure && block->inputs[1].midi.data1 == 64);
    CHECK(block->inputs[2].midi.type == RealtimeMidiEventType::PitchBend && block->inputs[2].midi.data1 == 8192);
    CHECK(block->inputs[3].midi.data1 == 16383);
  }
  // Capture file lifecycle: the producer hands off values only; destruction
  // drains the worker and closes a complete trace, including diagnostics.
  std::filesystem::path capturedPath;
  {
    auto capture = std::make_unique<MidiProbeCapture>(48000);
    capturedPath = capture->path();
    data.numSamples = 64; data.processContext = &context;
    context.projectTimeSamples = 4000; context.state = ProcessContext::kPlaying;
    events.events = {note(2, 127, 126)}; block->collect(data);
    capture->capture(*block, data, 48000);
    context.state = 0; events.events.clear(); block->collect(data);
    capture->capture(*block, data, 48000);
  }
  std::ifstream captureFile(capturedPath);
  std::string contents((std::istreambuf_iterator<char>(captureFile)), {});
  CHECK(contents.find("\"capture_only\":true") != std::string::npos);
  CHECK(contents.find("\"playing\":true") != std::string::npos);
  CHECK(contents.find("\"host_samples\":4002") != std::string::npos);
  CHECK(contents.find("\"data1\":127,\"data2\":126") != std::string::npos);
  CHECK(contents.find("\"capture_dropped\":0,\"blocks\":2") != std::string::npos);
  captureFile.close();
  // Remove only the two paths created by this test's unique capture instance.
  std::filesystem::remove(capturedPath);
  std::filesystem::remove(capturedPath.parent_path());
  std::cout << "PASS: " << passed << " FAIL: " << failed << '\n';
  return failed ? 1 : 0;
}
