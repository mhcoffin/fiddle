#pragma once

#include "midi_event.pb.h"

namespace fiddle {

/// Build the event used to replay one saved per-channel program after the
/// native plugin reconnects to FiddleServer.
inline MidiEvent makeProgramStateReplayEvent(int logicalChannel, int program) {
  MidiEvent event;
  event.set_timestamp_samples(0);
  event.set_port(logicalChannel / 16);
  event.set_channel(logicalChannel % 16 + 1);
  event.mutable_program_change()->set_program_number(program);
  return event;
}

} // namespace fiddle
