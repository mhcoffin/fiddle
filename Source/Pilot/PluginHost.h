#pragma once

#include "../PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace fiddle {

class PluginHost {
public:
  PluginHost() {}

  ~PluginHost() { processor.reset(); }

  bool loadPlugin(const juce::File &pluginFile) {
    juce::OwnedArray<juce::PluginDescription> descriptions;
    juce::KnownPluginList list;

    // Scan the specific file
    list.scanAndAddFile(pluginFile.getFullPathName(), true, descriptions,
                        *formatManager.getFormat(0));

    if (descriptions.size() > 0) {
      juce::String error;
      processor = formatManager.createPluginInstance(*descriptions[0], 44100.0,
                                                     512, error);

      if (processor != nullptr) {
        processor->prepareToPlay(44100.0, 512);
        return true;
      }
    }
    return false;
  }

  // If we can't find the VST3, we can also just use the class directly since
  // it's in our project
  void useInternalInstance() {
    processor = std::make_unique<FiddleAudioProcessor>();
    processor->prepareToPlay(44100.0, 512);
  }

  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) {
    if (processor != nullptr) {
      processor->processBlock(buffer, midiMessages);
    }
  }

  bool isConnected() const {
    if (auto *p = dynamic_cast<FiddleAudioProcessor *>(processor.get())) {
      return p->isConnected();
    }
    return false;
  }

private:
  juce::AudioPluginFormatManager formatManager;
  std::unique_ptr<juce::AudioProcessor> processor;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};

} // namespace fiddle
