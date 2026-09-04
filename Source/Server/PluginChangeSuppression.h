#pragma once

#include "midi_event.pb.h"

#include <cstdint>

namespace fiddle {

constexpr uint32_t kRecentPerformanceActivityWindowMs = 1000;

/// Events capable of changing a hosted plug-in as part of performance rather
/// than a persistent editor operation. Dorico note audition has no transport
/// lifecycle, so the MIDI events themselves must establish this context.
inline bool isPluginPerformanceActivity(const MidiEvent &event) {
  return event.has_note_on() || event.has_note_off() || event.has_cc() ||
         event.has_pitch_bend() || event.has_program_change() ||
         event.has_aftertouch() || event.has_channel_pressure() ||
         event.has_sys_ex() || event.has_other() || event.has_transport();
}

/// MillisecondCounter wraps naturally; signed subtraction is valid for the
/// short window used here.
inline bool hasRecentPluginPerformanceActivity(
    uint32_t now, uint32_t lastActivity,
    uint32_t windowMs = kRecentPerformanceActivityWindowMs) {
  if (lastActivity == 0)
    return false;
  const auto elapsed = static_cast<int32_t>(now - lastActivity);
  return elapsed >= 0 && static_cast<uint32_t>(elapsed) <= windowMs;
}

} // namespace fiddle
