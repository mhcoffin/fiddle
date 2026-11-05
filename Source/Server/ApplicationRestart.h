#pragma once

#include <juce_core/juce_core.h>

namespace fiddle::application_restart {

/// Resolve .../Application.app from .../Application.app/Contents/MacOS/binary.
[[nodiscard]] juce::File
findContainingAppBundle(const juce::File &executable);

/// Build the detached helper invocation used to wait for shutdown and reopen.
[[nodiscard]] juce::StringArray
makeMacHelperArguments(int processId, const juce::File &appBundle);

/// Start a helper that reopens the current app bundle after this process exits.
[[nodiscard]] bool schedule(const juce::File &executable);

/// Passed to the reopened app, so it can tell a Restart Fiddle relaunch from
/// an ordinary launch.
inline constexpr const char *kRelaunchedArgument = "--relaunched";

/// True when this launch came from Restart Fiddle.
[[nodiscard]] bool wasRelaunched(const juce::String &commandLine);

} // namespace fiddle::application_restart
