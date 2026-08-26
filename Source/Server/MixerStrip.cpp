#include "MixerStrip.h"

namespace fiddle {

MixerStrip::~MixerStrip() {
  if (lifetimeToken_)
    *lifetimeToken_ = false;
}

void MixerStrip::PluginChangeListener::audioProcessorParameterChanged(
    juce::AudioProcessor *, int, float) {
  if (listenerCapableUids && pluginUid != 0)
    listenerCapableUids->insert(pluginUid);
  if (onDirty)
    onDirty();
}

void MixerStrip::PluginChangeListener::audioProcessorChanged(
    juce::AudioProcessor *,
    const juce::AudioProcessorListener::ChangeDetails &d) {
  if (d.nonParameterStateChanged) {
    if (listenerCapableUids && pluginUid != 0)
      listenerCapableUids->insert(pluginUid);
    if (onDirty)
      onDirty();
  }
}

} // namespace fiddle
