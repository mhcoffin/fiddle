#include "../AudioDeviceSettings.h"
#include "../AudioDeviceSettingsStore.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

namespace {
// No CoreAudio devices, threads, or production settings are opened by this test.
class Device final : public juce::AudioIODevice {
public:
  Device() : AudioIODevice("Test output", "Test audio") {}
  juce::StringArray getOutputChannelNames() override { return {"Left", "Right"}; }
  juce::StringArray getInputChannelNames() override { return {}; }
  juce::Array<double> getAvailableSampleRates() override { return {44100, 48000}; }
  juce::Array<int> getAvailableBufferSizes() override { return {512, 1024}; }
  int getDefaultBufferSize() override { return 512; }
  juce::String open(const juce::BigInteger &, const juce::BigInteger &outputs,
                    double rate, int block) override {
    rate_ = rate; block_ = block; outputs_ = outputs; open_ = true; return {};
  }
  void close() override { stop(); open_ = false; }
  bool isOpen() override { return open_; }
  void start(juce::AudioIODeviceCallback *callback) override {
    stop(); callback_ = callback;
    if (callback_) callback_->audioDeviceAboutToStart(this);
  }
  void stop() override {
    if (callback_) callback_->audioDeviceStopped();
    callback_ = nullptr;
  }
  bool isPlaying() override { return callback_ != nullptr; }
  juce::String getLastError() override { return {}; }
  int getCurrentBufferSizeSamples() override { return block_; }
  double getCurrentSampleRate() override { return rate_; }
  int getCurrentBitDepth() override { return 32; }
  juce::BigInteger getActiveOutputChannels() const override { return outputs_; }
  juce::BigInteger getActiveInputChannels() const override { return {}; }
  int getOutputLatencyInSamples() override { return block_; }
  int getInputLatencyInSamples() override { return 0; }
private:
  juce::AudioIODeviceCallback *callback_ = nullptr;
  juce::BigInteger outputs_;
  bool open_ = false;
  double rate_ = 44100;
  int block_ = 512;
};
class DeviceType final : public juce::AudioIODeviceType {
public:
  DeviceType() : AudioIODeviceType("Test audio") {}
  void scanForDevices() override {}
  juce::StringArray getDeviceNames(bool input) const override {
    return input ? juce::StringArray{} : juce::StringArray{"Test output"};
  }
  int getDefaultDeviceIndex(bool) const override { return 0; }
  int getIndexOfDevice(juce::AudioIODevice *, bool) const override { return 0; }
  bool hasSeparateInputsAndOutputs() const override { return true; }
  juce::AudioIODevice *createDevice(const juce::String &output, const juce::String &) override {
    return output == "Test output" ? new Device() : nullptr;
  }
};
class Manager final : public juce::AudioDeviceManager {
public:
  void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType> &types) override {
    types.add(new DeviceType());
  }
};
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }
}

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("fiddle-device-test-" + juce::Uuid().toString());
  const auto file = dir.getChildFile("audio-device.xml");
  try {
    {
      Manager manager;
      fiddle::AudioDeviceSettings settings(manager, file);
      CHECK(settings.initialise().isEmpty());
      pump();
      CHECK(!file.exists()); // default startup must not persist a surprise choice
      CHECK(static_cast<int>(settings.diagnostics()["bufferSize"]) == 512);
      auto setup = manager.getAudioDeviceSetup();
      setup.bufferSize = 1024; setup.sampleRate = 48000;
      CHECK(manager.setAudioDeviceSetup(setup, true).isEmpty());
      pump();
      CHECK(file.existsAsFile());
      CHECK(static_cast<int>(settings.diagnostics()["bufferSize"]) == 1024);
    }
    {
      Manager manager;
      fiddle::AudioDeviceSettings settings(manager, file);
      CHECK(settings.initialise().isEmpty());
      CHECK(static_cast<int>(settings.diagnostics()["bufferSize"]) == 1024);
      CHECK(static_cast<double>(settings.diagnostics()["sampleRate"]) == 48000);
    }
    auto missing = juce::XmlDocument::parse(file);
    CHECK(missing != nullptr);
    missing->setAttribute("audioOutputDeviceName", "Unplugged interface");
    CHECK(fiddle::AudioDeviceSettingsStore::save(file, *missing));
    const auto original = file.loadFileAsString();
    {
      Manager manager;
      fiddle::AudioDeviceSettings settings(manager, file);
      CHECK(settings.initialise().isEmpty());
      pump();
      CHECK(settings.diagnostics()["warning"].toString().contains("unavailable"));
      CHECK(settings.diagnostics()["name"].toString() == "Test output");
      CHECK(file.loadFileAsString() == original); // fallback is not a new saved preference
    }
    CHECK(dir.deleteRecursively());
    std::cout << "Audio device: defaults, explicit changes, restart and missing-device fallback passed\n";
    return 0;
  } catch (const std::exception &e) {
    dir.deleteRecursively();
    std::cerr << e.what() << '\n'; return 1;
  }
}
