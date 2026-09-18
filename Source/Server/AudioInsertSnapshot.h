#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace fiddle {

/// Persistent identity and state for one hosted audio-effect instance.
struct AudioInsertSnapshot {
  juce::String slotId;
  juce::PluginDescription description;
  bool bypassed = false;
  juce::MemoryBlock pluginState;
};

enum class StripInsertPosition { preFader, postFader };

/// Complete ordered effect-rack and stereo-balance state for a strip or bus.
struct StripAudioSnapshot {
  std::vector<AudioInsertSnapshot> preFaderInserts;
  std::vector<AudioInsertSnapshot> postFaderInserts;
  float balance = 0.0f; // -1 left, 0 unchanged stereo, +1 right.
};

/// Compact self-describing representation used by versioned strip blobs and
/// the Dorico compatibility state blob. The live database stores the same
/// fields in normalized rows.
juce::MemoryBlock serializeStripAudioSnapshot(const StripAudioSnapshot &state);
StripAudioSnapshot deserializeStripAudioSnapshot(const void *data,
                                                 std::size_t size);

} // namespace fiddle
