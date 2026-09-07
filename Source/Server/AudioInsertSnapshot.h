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

/// Complete ordered effect-rack state for one instrument strip.
struct StripAudioSnapshot {
  std::vector<AudioInsertSnapshot> preFaderInserts;
  std::vector<AudioInsertSnapshot> postFaderInserts;
};

/// Compact self-describing representation used by versioned strip blobs and
/// the Dorico compatibility state blob. The live database stores the same
/// fields in normalized rows.
juce::MemoryBlock serializeStripAudioSnapshot(const StripAudioSnapshot &state);
StripAudioSnapshot deserializeStripAudioSnapshot(const void *data,
                                                 std::size_t size);

} // namespace fiddle
