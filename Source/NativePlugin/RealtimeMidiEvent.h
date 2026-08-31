#pragma once

#include "midi_event.pb.h"

#include <cstdint>
#include <string>
#include <type_traits>

namespace fiddle {

enum class RealtimeMidiEventType : uint8_t {
  NoteOn,
  NoteOff,
  PolyPressure,
  ControlChange,
  PitchBend,
  ChannelPressure,
  ProgramChange,
  Tempo,
  TransportStart,
  TransportStop,
  Other,
};

struct RealtimeMidiEvent {
  RealtimeMidiEventType type = RealtimeMidiEventType::Other;
  int32_t timestampSamples = 0;
  uint64_t hostSamplePosition = 0;
  int32_t port = 0;
  int32_t channel = 1;
  int32_t data1 = 0;
  int32_t data2 = 0;
  double tempo = 0.0;
};

static_assert(std::is_trivially_copyable_v<RealtimeMidiEvent>);

inline MidiEvent toMidiEvent(const RealtimeMidiEvent &source) {
  MidiEvent event;
  event.set_timestamp_samples(source.timestampSamples);
  event.set_host_sample_position(source.hostSamplePosition);
  event.set_port(source.port);
  event.set_channel(source.channel);

  switch (source.type) {
  case RealtimeMidiEventType::NoteOn: {
    auto *value = event.mutable_note_on();
    value->set_note_number(source.data1);
    value->set_velocity(source.data2);
    break;
  }
  case RealtimeMidiEventType::NoteOff: {
    auto *value = event.mutable_note_off();
    value->set_note_number(source.data1);
    value->set_velocity(source.data2);
    break;
  }
  case RealtimeMidiEventType::PolyPressure: {
    auto *value = event.mutable_aftertouch();
    value->set_note_number(source.data1);
    value->set_value(source.data2);
    break;
  }
  case RealtimeMidiEventType::ControlChange: {
    auto *value = event.mutable_cc();
    value->set_controller_number(source.data1);
    value->set_controller_value(source.data2);
    break;
  }
  case RealtimeMidiEventType::PitchBend:
    event.mutable_pitch_bend()->set_value(source.data1);
    break;
  case RealtimeMidiEventType::ChannelPressure:
    event.mutable_channel_pressure()->set_value(source.data1);
    break;
  case RealtimeMidiEventType::ProgramChange:
    event.mutable_program_change()->set_program_number(source.data1);
    break;
  case RealtimeMidiEventType::Tempo: {
    auto *value = event.mutable_tempo();
    value->set_bpm(source.tempo);
    value->set_time_sig_numerator(source.data1);
    value->set_time_sig_denominator(source.data2);
    break;
  }
  case RealtimeMidiEventType::TransportStart:
  case RealtimeMidiEventType::TransportStop: {
    auto *value = event.mutable_transport();
    value->set_type(source.type == RealtimeMidiEventType::TransportStart
                        ? MidiEvent_TransportEvent_Type_START
                        : MidiEvent_TransportEvent_Type_STOP);
    value->set_host_sample_position(source.hostSamplePosition);
    break;
  }
  case RealtimeMidiEventType::Other:
    event.mutable_other()->set_description("Unsupported VST3 event type " +
                                           std::to_string(source.data1));
    break;
  }

  return event;
}

} // namespace fiddle
