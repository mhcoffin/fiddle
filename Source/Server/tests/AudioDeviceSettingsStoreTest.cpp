#include "../AudioDeviceSettingsStore.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main() {
  const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("fiddle-audio-settings-test-" + juce::Uuid().toString());
  try {
    const auto file = dir.getChildFile("audio-device.xml");
    juce::String error;
    CHECK(!fiddle::AudioDeviceSettingsStore::load(file, error) && error.isEmpty());
    CHECK(!dir.exists()); // reading defaults never creates settings
    juce::XmlElement xml("DEVICESETUP");
    xml.setAttribute("deviceType", "CoreAudio");
    xml.setAttribute("audioOutputDeviceName", "Test output");
    xml.setAttribute("audioDeviceRate", 44100);
    xml.setAttribute("audioDeviceBufferSize", 512);
    CHECK(fiddle::AudioDeviceSettingsStore::save(file, xml));
    auto saved = fiddle::AudioDeviceSettingsStore::load(file, error);
    CHECK(saved && error.isEmpty() && saved->isEquivalentTo(&xml, false));
    xml.setAttribute("audioDeviceBufferSize", 1024);
    CHECK(fiddle::AudioDeviceSettingsStore::save(file, xml));
    saved = fiddle::AudioDeviceSettingsStore::load(file, error);
    CHECK(saved && saved->getIntAttribute("audioDeviceBufferSize") == 1024);
    const auto contents = file.loadFileAsString();
    CHECK(!fiddle::AudioDeviceSettingsStore::save(file, juce::XmlElement("wrong")));
    CHECK(file.loadFileAsString() == contents);
    CHECK(file.replaceWithText("<broken"));
    CHECK(!fiddle::AudioDeviceSettingsStore::load(file, error) && error.isNotEmpty());
    CHECK(file.loadFileAsString() == "<broken"); // failed reads preserve the original
    CHECK(dir.deleteRecursively());
    std::cout << "Audio settings: defaults, persistence, updates and invalid files passed\n";
    return 0;
  } catch (const std::exception &e) {
    dir.deleteRecursively();
    std::cerr << e.what() << '\n';
    return 1;
  }
}
