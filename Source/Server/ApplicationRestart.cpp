#include "ApplicationRestart.h"

#if JUCE_MAC
#include <unistd.h>
#endif

namespace fiddle::application_restart {

juce::File findContainingAppBundle(const juce::File &executable) {
  const auto appBundle = executable.getParentDirectory()
                             .getParentDirectory()
                             .getParentDirectory();
  return appBundle.hasFileExtension("app") ? appBundle : juce::File{};
}

juce::StringArray makeMacHelperArguments(int processId,
                                         const juce::File &appBundle) {
  return {
      "/bin/sh",
      "-c",
      "while /bin/kill -0 \"$1\" 2>/dev/null; do /bin/sleep 0.1; done; "
      "exec /usr/bin/open -n \"$2\"",
      "fiddle-restart",
      juce::String(processId),
      appBundle.getFullPathName(),
  };
}

bool schedule(const juce::File &executable) {
#if JUCE_MAC
  const auto appBundle = findContainingAppBundle(executable);
  if (!appBundle.isDirectory())
    return false;

  juce::ChildProcess helper;
  return helper.start(makeMacHelperArguments(static_cast<int>(::getpid()),
                                             appBundle),
                      0);
#else
  juce::ignoreUnused(executable);
  return false;
#endif
}

} // namespace fiddle::application_restart
