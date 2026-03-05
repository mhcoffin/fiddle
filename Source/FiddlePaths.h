#pragma once

/**
 * Cross-platform path helpers that work around the JUCE bug where
 * juce::File::userApplicationDataDirectory resolves to ~/Library on macOS
 * instead of the Apple-standard ~/Library/Application Support.
 *
 * See: https://forum.juce.com/t/userApplicationDataDirectory-on-mac
 *
 * Use these helpers instead of calling
 * juce::File::getSpecialLocation(userApplicationDataDirectory) directly.
 */

#if __has_include(<juce_core/juce_core.h>)
// ── JUCE-aware wrapper (server and JUCE-linked targets) ──────────────
#include <juce_core/juce_core.h>

namespace fiddle {

/// Returns ~/Library/Application Support on macOS,
/// JUCE's userApplicationDataDirectory on other platforms.
inline juce::File getUserAppSupportDir() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
  dir = dir.getChildFile("Application Support");
#endif
  return dir;
}

} // namespace fiddle

#else
// ── POSIX-only wrapper (native plugin, no JUCE dependency) ───────────
#include <cstdlib>
#include <pwd.h>
#include <string>
#include <unistd.h>

namespace fiddle {

/// Returns the user's home directory via $HOME or getpwuid.
inline std::string getHomeDirStr() {
  const char *home = std::getenv("HOME");
  if (home)
    return home;
  struct passwd *pw = getpwuid(getuid());
  if (pw)
    return pw->pw_dir;
  return "/tmp";
}

/// Returns ~/Library/Application Support (macOS).
inline std::string getUserAppSupportPath() {
#ifdef __APPLE__
  return getHomeDirStr() + "/Library/Application Support";
#else
  // XDG_DATA_HOME or ~/.local/share on Linux
  const char *xdg = std::getenv("XDG_DATA_HOME");
  if (xdg)
    return xdg;
  return getHomeDirStr() + "/.local/share";
#endif
}

} // namespace fiddle

#endif
