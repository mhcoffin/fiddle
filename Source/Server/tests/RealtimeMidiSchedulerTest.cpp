#include "RealtimeMidiScheduler.h"

#include <iostream>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                     \
                << "]: " << #condition << std::endl;                           \
    }                                                                          \
  } while (false)

struct CapturedEvent {
  juce::MidiMessage message;
  int sampleOffset = 0;
};

std::vector<CapturedEvent> capture(const juce::MidiBuffer &buffer) {
  std::vector<CapturedEvent> result;
  for (const auto metadata : buffer)
    result.push_back({metadata.getMessage(), metadata.samplePosition});
  return result;
}

void testSampleAccurateSchedulingAndRetention() {
  fiddle::RealtimeMidiScheduler scheduler;
  juce::MidiBuffer block;

  CHECK(scheduler.schedule(1000.0, juce::MidiMessage::noteOn(1, 60, 0.8f)));
  CHECK(scheduler.schedule(1005.0, juce::MidiMessage::noteOn(1, 61, 0.8f)));
  CHECK(scheduler.schedule(1011.0, juce::MidiMessage::noteOff(1, 60)));

  scheduler.renderBlock(1000.0, 48000.0, 512, block);
  auto events = capture(block);
  CHECK(events.size() == 2);
  CHECK(events[0].message.isNoteOn() &&
        events[0].message.getNoteNumber() == 60);
  CHECK(events[0].sampleOffset == 0);
  CHECK(events[1].message.isNoteOn() &&
        events[1].message.getNoteNumber() == 61);
  CHECK(events[1].sampleOffset == 240);

  block.clear();
  scheduler.renderBlock(1010.0, 48000.0, 512, block);
  events = capture(block);
  CHECK(events.size() == 1);
  CHECK(events[0].message.isNoteOff() &&
        events[0].message.getNoteNumber() == 60);
  CHECK(events[0].sampleOffset == 48);
}

void testClearDropsQueuedAndPendingEvents() {
  fiddle::RealtimeMidiScheduler scheduler;
  juce::MidiBuffer block;

  scheduler.schedule(2000.0, juce::MidiMessage::noteOn(1, 64, 0.8f));
  scheduler.renderBlock(1000.0, 48000.0, 512, block);
  CHECK(block.isEmpty());

  scheduler.schedule(2001.0, juce::MidiMessage::noteOn(1, 65, 0.8f));
  scheduler.requestClear();
  scheduler.renderBlock(1999.0, 48000.0, 512, block);
  CHECK(block.isEmpty());
}

void testPanicClearsNotesAndEmitsControllerResets() {
  fiddle::RealtimeMidiScheduler scheduler;
  juce::MidiBuffer block;

  scheduler.schedule(3000.0, juce::MidiMessage::noteOn(1, 67, 0.8f));
  scheduler.requestPanic();
  scheduler.renderBlock(3000.0, 48000.0, 512, block);

  const auto events = capture(block);
  CHECK(events.size() == 48);
  int allSoundOff = 0;
  int allNotesOff = 0;
  int resetControllers = 0;
  for (const auto &event : events) {
    CHECK(event.sampleOffset == 0);
    CHECK(!event.message.isNoteOn());
    if (event.message.isAllSoundOff())
      ++allSoundOff;
    if (event.message.isAllNotesOff())
      ++allNotesOff;
    if (event.message.isResetAllControllers())
      ++resetControllers;
  }
  CHECK(allSoundOff == 16);
  CHECK(allNotesOff == 16);
  CHECK(resetControllers == 16);
}

void testUnsupportedMessagesAreCounted() {
  fiddle::RealtimeMidiScheduler scheduler;
  const uint8_t payload[] = {1, 2, 3, 4};
  CHECK(!scheduler.schedule(0.0,
                            juce::MidiMessage::createSysExMessage(payload, 4)));
  CHECK(scheduler.droppedMessageCount() == 1);
}

} // namespace

int main() {
  testSampleAccurateSchedulingAndRetention();
  testClearDropsQueuedAndPendingEvents();
  testPanicClearsNotesAndEmitsControllerResets();
  testUnsupportedMessagesAreCounted();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
