#include "FiddleCIDs.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
#define REQUIRE(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (false)
static std::set<std::filesystem::path> captureDirectories() {
  std::set<std::filesystem::path> result;
  for (auto &entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path()))
    if (entry.path().filename().string().find("fiddle-midi-probe-") == 0) result.insert(entry.path());
  return result;
}
int main(int argc, char **argv) {
  REQUIRE(argc == 2);
  auto before = captureDirectories();
  auto path = std::filesystem::absolute(argv[1]).string();
  auto url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8 *>(path.data()), path.size(), true);
  auto bundle = CFBundleCreate(nullptr, url); CFRelease(url);
  REQUIRE(bundle && CFBundleLoadExecutable(bundle));
  auto entry = reinterpret_cast<bool (*)(CFBundleRef)>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
  auto exit = reinterpret_cast<bool (*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
  auto getFactory = reinterpret_cast<IPluginFactory *(*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
  REQUIRE(entry && exit && getFactory && entry(bundle));
  auto *factory = getFactory(); REQUIRE(factory && factory->countClasses() == 2);
  PClassInfo info{}; REQUIRE(factory->getClassInfo(0, &info) == kResultOk);
  REQUIRE(std::strcmp(info.name, "Fiddle MIDI Probe") == 0);
  REQUIRE(std::memcmp(info.cid, fiddle::kFiddleProcessorUID, sizeof(TUID)) == 0);
  IComponent *component = nullptr;
  REQUIRE(factory->createInstance(info.cid, IComponent::iid, reinterpret_cast<void **>(&component)) == kResultOk);
  REQUIRE(component && component->initialize(nullptr) == kResultOk);
  IAudioProcessor *processor = nullptr;
  REQUIRE(component->queryInterface(IAudioProcessor::iid, reinterpret_cast<void **>(&processor)) == kResultOk);
  ProcessSetup setup{}; setup.processMode = kRealtime; setup.symbolicSampleSize = kSample32;
  setup.maxSamplesPerBlock = 64; setup.sampleRate = 48000;
  REQUIRE(processor->setupProcessing(setup) == kResultOk);
  REQUIRE(processor->getLatencySamples() == 0);
  REQUIRE(component->setActive(true) == kResultOk);
  const auto startProcessing = processor->setProcessing(true);
  REQUIRE(startProcessing == kResultOk || startProcessing == kNotImplemented);
  float left[64], right[64]; std::fill_n(left, 64, 1.0f); std::fill_n(right, 64, -1.0f);
  float *channels[] = {left, right};
  AudioBusBuffers output{}; output.numChannels = 2; output.channelBuffers32 = channels;
  ProcessContext context{}; context.state = ProcessContext::kPlaying; context.projectTimeSamples = 1234;
  ProcessData data{}; data.numSamples = 64; data.symbolicSampleSize = kSample32;
  data.numOutputs = 1; data.outputs = &output; data.processContext = &context;
  REQUIRE(processor->process(data) == kResultOk);
  REQUIRE(std::all_of(left, left + 64, [](float v) { return v == 0; }));
  REQUIRE(std::all_of(right, right + 64, [](float v) { return v == 0; }));
  REQUIRE((output.silenceFlags & 3) == 3);
  const auto stopProcessing = processor->setProcessing(false);
  REQUIRE(stopProcessing == kResultOk || stopProcessing == kNotImplemented);
  REQUIRE(component->setActive(false) == kResultOk);
  REQUIRE(component->terminate() == kResultOk);
  processor->release(); component->release(); factory->release();
  REQUIRE(exit()); CFBundleUnloadExecutable(bundle); CFRelease(bundle);
  auto after = captureDirectories();
  std::vector<std::filesystem::path> created;
  std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(created));
  REQUIRE(created.size() == 1);
  auto capture = created[0] / "capture.jsonl";
  std::ifstream input(capture);
  std::string text((std::istreambuf_iterator<char>(input)), {});
  REQUIRE(text.find("\"capture_only\":true") != std::string::npos);
  REQUIRE(text.find("\"host_samples\":1234") != std::string::npos);
  REQUIRE(text.find("\"capture_dropped\":0,\"blocks\":1") != std::string::npos);
  input.close(); std::filesystem::remove(capture); std::filesystem::remove(created[0]);
  std::cout << "Probe bundle load, identity, activation, silence, capture and shutdown: PASS\n";
}
