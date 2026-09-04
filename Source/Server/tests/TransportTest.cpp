#include "../NoteStreamTracker.h"
#include "../PluginChangeSuppression.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition) {                                                           \
      ++passed;                                                                \
    } else {                                                                   \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                   \
                << "]: " #condition << std::endl;                            \
    }                                                                          \
  } while (false)

fiddle::MidiEvent makeCC(uint32_t channel, uint32_t controller,
                         uint32_t value) {
  fiddle::MidiEvent event;
  event.set_channel(channel);
  auto *cc = event.mutable_cc();
  cc->set_controller_number(controller);
  cc->set_controller_value(value);
  return event;
}

fiddle::MidiEvent makeNoteOn(uint32_t channel, uint32_t note,
                             uint64_t samplePosition) {
  fiddle::MidiEvent event;
  event.set_channel(channel);
  event.set_host_sample_position(samplePosition);
  auto *noteOn = event.mutable_note_on();
  noteOn->set_note_number(note);
  noteOn->set_velocity(100);
  return event;
}

fiddle::MidiEvent makeNoteOff(uint32_t channel, uint32_t note,
                              uint64_t samplePosition) {
  fiddle::MidiEvent event;
  event.set_channel(channel);
  event.set_host_sample_position(samplePosition);
  auto *noteOff = event.mutable_note_off();
  noteOff->set_note_number(note);
  noteOff->set_velocity(64);
  return event;
}

fiddle::MidiEvent
makeTransport(fiddle::MidiEvent_TransportEvent_Type type) {
  fiddle::MidiEvent event;
  event.mutable_transport()->set_type(type);
  return event;
}

void testCCStateUsesOneBasedChannels() {
  fiddle::NoteStreamTracker tracker;

  tracker.processEvent(makeCC(1, 1, 73));
  tracker.processEvent(makeCC(16, 1, 109));

  CHECK(tracker.getCC(1, 1) == 73);
  CHECK(tracker.getCC(16, 1) == 109);
  CHECK(tracker.getCC(0, 1) == 0);
  CHECK(tracker.getCC(17, 1) == 0);
}

void testTransportDoesNotDiscardActiveNotes() {
  fiddle::NoteStreamTracker tracker;
  int transportEvents = 0;
  tracker.setCallbacks({
      {},
      {},
      {},
      [&transportEvents](const fiddle::MidiEvent &event, uint64_t, int) {
        if (event.has_transport())
          ++transportEvents;
      },
  });

  tracker.processEvent(makeNoteOn(1, 60, 1000));
  CHECK(tracker.getActiveNotes().size() == 1);

  tracker.processEvent(
      makeTransport(fiddle::MidiEvent_TransportEvent_Type_START));
  CHECK(tracker.getActiveNotes().size() == 1);

  tracker.processEvent(
      makeTransport(fiddle::MidiEvent_TransportEvent_Type_STOP));
  CHECK(tracker.getActiveNotes().size() == 1);
  CHECK(transportEvents == 2);
}

void testOutOfOrderNoteOffIsClamped() {
  fiddle::NoteStreamTracker tracker;
  fiddle::Note endedNote;
  bool ended = false;
  tracker.setCallbacks({
      {},
      [&endedNote, &ended](const fiddle::Note &note) {
        endedNote = note;
        ended = true;
      },
      {},
      {},
  });

  tracker.processEvent(makeNoteOn(16, 67, 2000));
  tracker.processEvent(makeNoteOff(16, 67, 1500));

  CHECK(ended);
  CHECK(endedNote.channel() == 16);
  CHECK(endedNote.start_sample() == 2000);
  CHECK(endedNote.duration_samples() == 0);
  CHECK(tracker.getActiveNotes().empty());
}

void testPluginPlaybackActivityIncludesNoteAuditionAndPreTransportMidi() {
  const auto note = makeNoteOn(1, 60, 1000);
  const auto cc = makeCC(1, 1, 72);
  const auto transport =
      makeTransport(fiddle::MidiEvent_TransportEvent_Type_START);
  fiddle::MidiEvent tempo;
  tempo.mutable_tempo()->set_bpm(120.0);
  fiddle::MidiEvent save;
  save.mutable_save_config_request()->set_request_id(1);

  CHECK(fiddle::isPluginPerformanceActivity(note));
  CHECK(fiddle::isPluginPerformanceActivity(cc));
  CHECK(fiddle::isPluginPerformanceActivity(transport));
  CHECK(!fiddle::isPluginPerformanceActivity(tempo));
  CHECK(!fiddle::isPluginPerformanceActivity(save));

  CHECK(fiddle::hasRecentPluginPerformanceActivity(1200, 1000));
  CHECK(fiddle::hasRecentPluginPerformanceActivity(2000, 1000));
  CHECK(!fiddle::hasRecentPluginPerformanceActivity(2001, 1000));
  CHECK(!fiddle::hasRecentPluginPerformanceActivity(1000, 0));

  // Verify the unsigned millisecond counter wrap-around case.
  CHECK(fiddle::hasRecentPluginPerformanceActivity(20, 0xfffffff0u));
}

} // namespace

int main() {
  std::cout << "===== Transport Tests =====" << std::endl;

  testCCStateUsesOneBasedChannels();
  testTransportDoesNotDiscardActiveNotes();
  testOutOfOrderNoteOffIsClamped();
  testPluginPlaybackActivityIncludesNoteAuditionAndPreTransportMidi();

  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
