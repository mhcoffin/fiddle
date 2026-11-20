#pragma once

// What a project's sound depends on outside its saved Fiddle version: the
// Fiddle release, the audio sample rate, the plug-ins it uses and the Fiddle
// scripts (Lua processors) on its layers. Fiddle records this whenever the
// project is saved and compares it when a Dorico project reopens, so the
// restore banner can say what has changed since (docs/dorico-project-identity-results.md).

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <vector>

namespace fiddle {

struct ProjectEnvironment {
  struct Plugin {
    juce::String name, manufacturer;
    juce::String version;     // the bundle's own version (CFBundleShortVersionString)
    juce::int64 fileTime = 0; // the bundle's modification time (ms), 0 if unknown
    juce::String path;
    bool instrument = false;  // played on a layer (an effect otherwise)
  };
  struct Script {
    juce::String filePath;
    juce::String fingerprint; // of the file's text
  };

  juce::String appVersion;
  double sampleRate = 0.0;              // 0 when no audio device is running
  std::map<int, Plugin> plugins;        // by plug-in unique ID
  std::map<juce::String, Script> scripts; // by script name

  juce::var toJson() const;
  static std::optional<ProjectEnvironment> fromJson(const juce::var &json);

  static juce::String fingerprintOf(const juce::String &text);
};

/// What changed between the environment a project was saved with and now,
/// as sentences for the user, most significant first. A missing instrument or
/// script isn't listed: the restore banner reports those from the mixer as
/// layers that can't play. A missing effect is listed, since nothing else
/// reports an insert that's gone.
std::vector<juce::String> environmentChanges(const ProjectEnvironment &saved,
                                             const ProjectEnvironment &now);

} // namespace fiddle
