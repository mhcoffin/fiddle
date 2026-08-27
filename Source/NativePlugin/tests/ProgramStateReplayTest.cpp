#include "../ProgramStateReplay.h"

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

} // namespace

int main() {
  std::cout << "===== Program State Replay Tests =====" << std::endl;
  testReplayUsesZeroBasedPortsAndOneBasedChannels();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
