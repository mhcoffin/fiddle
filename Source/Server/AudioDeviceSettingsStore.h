#pragma once
#include <juce_core/juce_core.h>

namespace fiddle {
// Machine-local preferences, deliberately separate from versioned mixer state.
struct AudioDeviceSettingsStore {
  static std::unique_ptr<juce::XmlElement> load(const juce::File &file,
                                               juce::String &error) {
    error.clear();
    if (!file.existsAsFile()) return {};
    auto xml = juce::XmlDocument::parse(file);
    if (!xml || !xml->hasTagName("DEVICESETUP")) {
      error = "Saved audio settings could not be read; using the default device.";
      return {};
    }
    return xml;
  }
  static bool save(const juce::File &file, const juce::XmlElement &xml) {
    if (!xml.hasTagName("DEVICESETUP")) return false;
    const auto text = xml.toString();
    if (file.existsAsFile() && file.loadFileAsString() == text) return true;
    if (file.getParentDirectory().createDirectory().failed()) return false;
    juce::TemporaryFile temporary(file);
    return temporary.getFile().replaceWithText(text) && temporary.overwriteTargetFileWithTemporary();
  }
};
} // namespace fiddle
