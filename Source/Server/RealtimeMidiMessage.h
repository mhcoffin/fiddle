#pragma once

#include <cstdint>
#include <type_traits>

namespace fiddle {

/// Queue representation for the short channel-voice messages routed by the
/// server. Keeping this as POD avoids juce::MidiMessage's heap-backed cases.
struct RealtimeMidiMessage {
  double triggerTimeMs = 0.0;
  uint8_t size = 0;
  uint8_t data[3]{};
};

static_assert(std::is_trivially_copyable_v<RealtimeMidiMessage>);

} // namespace fiddle
