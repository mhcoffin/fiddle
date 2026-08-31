#include "../ProgramStateReplay.h"
#include "../RealtimeMidiEvent.h"

#include <iostream>

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

void checkAddress(int logicalChannel, int expectedPort, int expectedChannel) {
  auto event = fiddle::makeProgramStateReplayEvent(logicalChannel, 42);
  CHECK(event.port() == expectedPort);
  CHECK(event.channel() == expectedChannel);
  CHECK(event.program_change().program_number() == 42);
}

void testReplayUsesZeroBasedPortsAndOneBasedChannels() {
  checkAddress(0, 0, 1);
  checkAddress(15, 0, 16);
  checkAddress(16, 1, 1);
  checkAddress(255, 15, 16);
}

void testRealtimeEventConversionPreservesAddressAndTiming() {
  fiddle::RealtimeMidiEvent source;
  source.type = fiddle::RealtimeMidiEventType::NoteOn;
  source.timestampSamples = 37;
  source.hostSamplePosition = 123456;
  source.port = 4;
  source.channel = 12;
  source.data1 = 61;
  source.data2 = 99;

  auto event = fiddle::toMidiEvent(source);
  CHECK(event.timestamp_samples() == 37);
  CHECK(event.host_sample_position() == 123456);
  CHECK(event.port() == 4);
  CHECK(event.channel() == 12);
  CHECK(event.note_on().note_number() == 61);
  CHECK(event.note_on().velocity() == 99);
}

void testRealtimeTransportConversion() {
  fiddle::RealtimeMidiEvent source;
  source.type = fiddle::RealtimeMidiEventType::TransportStop;
  source.hostSamplePosition = 98765;

  auto event = fiddle::toMidiEvent(source);
  CHECK(event.transport().type() ==
        fiddle::MidiEvent_TransportEvent_Type_STOP);
  CHECK(event.transport().host_sample_position() == 98765);
}

} // namespace

int main() {
  std::cout << "===== Program State Replay Tests =====" << std::endl;
  testReplayUsesZeroBasedPortsAndOneBasedChannels();
  testRealtimeEventConversionPreservesAddressAndTiming();
  testRealtimeTransportConversion();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
