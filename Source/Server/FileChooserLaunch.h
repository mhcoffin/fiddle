#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>

namespace fiddle {

/// Launch a file chooser and, when it closes (chosen or cancelled), bring
/// back the Fiddle window that was in front when it opened.
///
/// JUCE's macOS chooser is a free-floating NSOpenPanel/NSSavePanel; it
/// ignores FileChooser's parent component. When the panel closes, macOS makes
/// the app's main window key, so a Library Manager or tool window that asked
/// for the file dropped behind the mixer.
inline void launchFileChooser(juce::FileChooser &chooser, int flags,
                              std::function<void(const juce::FileChooser &)> callback) {
  juce::Component::SafePointer<juce::Component> requester(
      juce::TopLevelWindow::getActiveTopLevelWindow());
  chooser.launchAsync(flags, [requester, callback = std::move(callback)](const juce::FileChooser &fc) {
    if (requester != nullptr && requester->isShowing())
      requester->toFront(true);
    if (callback)
      callback(fc);
  });
}

} // namespace fiddle
