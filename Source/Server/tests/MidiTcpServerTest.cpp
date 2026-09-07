#include "../MidiTcpServer.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition) {                                                           \
      ++passed;                                                                \
    } else {                                                                   \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                  \
                << "]: " #condition << std::endl;                             \
    }                                                                          \
  } while (false)

template <typename Predicate>
bool waitUntil(Predicate predicate,
               std::chrono::milliseconds timeout =
                   std::chrono::milliseconds(2000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

bool connectWithRetry(juce::StreamingSocket &socket, int port) {
  return waitUntil([&] { return socket.connect("127.0.0.1", port, 100); });
}

bool sendEvent(juce::StreamingSocket &socket,
               const fiddle::MidiEvent &event) {
  std::string bytes;
  if (!event.SerializeToString(&bytes))
    return false;

  const auto size = static_cast<uint32_t>(bytes.size());
  const auto networkSize = juce::ByteOrder::swapIfLittleEndian(size);
  return socket.write(&networkSize, 4) == 4 &&
         socket.write(bytes.data(), static_cast<int>(bytes.size())) ==
             static_cast<int>(bytes.size());
}

void testAdditionalConnectionIsReportedWithoutReplacingPrimary() {
  constexpr int port = 55252;
  fiddle::MidiTcpServer server(port);
  std::atomic<int> connected{0};
  std::atomic<int> additional{0};
  std::atomic<int> received{0};

  server.onConnectionChanged([&](bool isConnected, const juce::String &) {
    if (isConnected)
      connected.fetch_add(1);
  });
  server.onAdditionalConnectionAttempt(
      [&](const juce::String &) { additional.fetch_add(1); });
  server.onMessageReceived(
      [&](const fiddle::MidiEvent &) { received.fetch_add(1); });
  server.startThread();

  juce::StreamingSocket primary;
  CHECK(connectWithRetry(primary, port));
  CHECK(waitUntil([&] { return connected.load() == 1; }));

  juce::StreamingSocket extra;
  CHECK(extra.connect("127.0.0.1", port, 1000));
  CHECK(waitUntil([&] { return additional.load() == 1; }));
  CHECK(connected.load() == 1);

  fiddle::MidiEvent event;
  event.set_channel(1);
  event.mutable_note_on()->set_note_number(60);
  event.mutable_note_on()->set_velocity(100);
  CHECK(sendEvent(primary, event));
  CHECK(waitUntil([&] { return received.load() == 1; }));

  // Reconnect attempts from the rejected plug-in must not repeat the warning
  // during the same primary-client session.
  juce::StreamingSocket retry;
  CHECK(retry.connect("127.0.0.1", port, 1000));
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  CHECK(additional.load() == 1);

  retry.close();
  extra.close();
  primary.close();
}

} // namespace

int main() {
  std::cout << "===== MIDI TCP Server Tests =====" << std::endl;
  testAdditionalConnectionIsReportedWithoutReplacingPrimary();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
