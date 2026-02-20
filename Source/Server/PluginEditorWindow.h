#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace fiddle {

/// A floating window that hosts a VST3 plugin's native editor UI.
class PluginEditorWindow : public juce::DocumentWindow {
public:
  PluginEditorWindow(const juce::String &pluginName,
                     juce::AudioProcessorEditor *editor,
                     std::function<void()> onCloseCallback = {})
      : juce::DocumentWindow(pluginName, juce::Colours::darkgrey,
                             juce::DocumentWindow::allButtons),
        onClose_(std::move(onCloseCallback)) {
    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(editor->isResizable(), false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
    toFront(true);
  }

  void closeButtonPressed() override {
    setVisible(false);
    if (onClose_)
      onClose_();
  }

private:
  std::function<void()> onClose_;
};

} // namespace fiddle
