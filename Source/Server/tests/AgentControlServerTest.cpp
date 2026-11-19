#include "../AgentControlServer.h"

#include <chrono>
#include <iostream>
#include <sys/stat.h>
#include <thread>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                \
                << "]: " #condition << std::endl;                            \
    }                                                                          \
  } while (false)

template <typename Predicate> bool waitUntil(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

juce::var readDescriptor(const juce::File &file) {
  return juce::JSON::parse(file);
}

juce::var request(int port, const juce::String &token,
                  const juce::String &method) {
  juce::StreamingSocket socket;
  if (!socket.connect("127.0.0.1", port, 1000)) {
    std::cerr << "Client connect failed on port " << port << std::endl;
    return {};
  }
  auto *message = new juce::DynamicObject();
  message->setProperty("id", "test-1");
  message->setProperty("token", token);
  message->setProperty("method", method);
  message->setProperty("params", juce::var(new juce::DynamicObject()));
  const auto body = juce::JSON::toString(juce::var(message), true) + "\n";
  const auto written = socket.write(body.toRawUTF8(), body.getNumBytesAsUTF8());
  if (written != body.getNumBytesAsUTF8()) {
    std::cerr << "Client write failed: " << written << std::endl;
    return {};
  }

  std::string response;
  char buffer[4096];
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (socket.waitUntilReady(true, 100) <= 0)
      continue;
    const int count = socket.read(buffer, sizeof(buffer), false);
    if (count <= 0)
      break;
    response.append(buffer, static_cast<size_t>(count));
    if (response.find('\n') != std::string::npos)
      break;
  }
  if (const auto newline = response.find('\n'); newline != std::string::npos)
    response.resize(newline);
  return juce::JSON::parse(juce::String::fromUTF8(response.c_str()));
}

void testAuthenticatedRequestAndDescriptorLifecycle() {
  auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getNonexistentChildFile("fiddle-agent-control", {}, false);
  temp.createDirectory();
  const auto descriptor = temp.getChildFile("agent-control.json");
  std::atomic<int> handled{0};
  {
    fiddle::AgentControlServer server(
        descriptor,
        [&handled](const juce::String &method, const juce::var &,
                   fiddle::AgentControlServer::Completion complete) {
          handled.fetch_add(1);
          auto *result = new juce::DynamicObject();
          result->setProperty("method", method);
          // Larger than one send() can take, so the bridge must keep writing.
          if (method == "large")
            result->setProperty("payload", juce::String::repeatedString("x", 600000));
          complete(fiddle::AgentControlServer::Response::success(
              juce::var(result)));
        });
    server.startThread();
    CHECK(waitUntil([&] {
      return server.listeningPort() > 0 && descriptor.existsAsFile();
    }));
    const auto info = readDescriptor(descriptor);
    CHECK(info["host"].toString() == "127.0.0.1");
    CHECK(static_cast<int>(info["port"]) == server.listeningPort());
    CHECK(info["token"].toString().isNotEmpty());
#if JUCE_MAC || JUCE_LINUX
    struct stat fileInfo {};
    CHECK(::stat(descriptor.getFullPathName().toRawUTF8(), &fileInfo) == 0);
    CHECK((fileInfo.st_mode & 0777) == 0600);
#endif

    const auto valid = request(server.listeningPort(),
                               info["token"].toString(), "status");
    CHECK(handled.load() == 1);
    if (!static_cast<bool>(valid["ok"]))
      std::cerr << "Valid response: " << juce::JSON::toString(valid, true)
                << std::endl;
    CHECK(static_cast<bool>(valid["ok"]));
    CHECK(valid["result"]["method"].toString() == "status");

    const auto large = request(server.listeningPort(),
                               info["token"].toString(), "large");
    CHECK(static_cast<bool>(large["ok"]));
    CHECK(large["result"]["payload"].toString().length() == 600000);

    const auto invalid = request(server.listeningPort(), "wrong", "status");
    if (invalid["error"].toString() != "Authentication failed")
      std::cerr << "Invalid response: " << juce::JSON::toString(invalid, true)
                << std::endl;
    CHECK(!static_cast<bool>(invalid["ok"]));
    CHECK(invalid["error"].toString() == "Authentication failed");
  }
  CHECK(!descriptor.existsAsFile());
  temp.deleteRecursively();
}

// A client that asks for a large answer and stops reading must not hold up
// shutdown: the write loop gives up when the server thread is told to exit.
void testShutdownWhileClientStopsReading() {
  auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getNonexistentChildFile("fiddle-agent-control", {}, false);
  temp.createDirectory();
  const auto descriptor = temp.getChildFile("agent-control.json");
  auto server = std::make_unique<fiddle::AgentControlServer>(
      descriptor,
      [](const juce::String &, const juce::var &,
         fiddle::AgentControlServer::Completion complete) {
        auto *result = new juce::DynamicObject();
        result->setProperty("payload", juce::String::repeatedString("x", 8 * 1024 * 1024));
        complete(fiddle::AgentControlServer::Response::success(juce::var(result)));
      });
  server->startThread();
  CHECK(waitUntil([&] { return server->listeningPort() > 0; }));
  const auto token = readDescriptor(descriptor)["token"].toString();

  juce::StreamingSocket client;
  CHECK(client.connect("127.0.0.1", server->listeningPort(), 1000));
  auto *message = new juce::DynamicObject();
  message->setProperty("id", "stalled");
  message->setProperty("token", token);
  message->setProperty("method", "large");
  const auto body = juce::JSON::toString(juce::var(message), true) + "\n";
  CHECK(client.write(body.toRawUTF8(), body.getNumBytesAsUTF8()) ==
        static_cast<int>(body.getNumBytesAsUTF8()));
  // Never read: 8 MB is more than loopback buffers hold, so the server waits.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  const auto start = std::chrono::steady_clock::now();
  server.reset();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  if (elapsed.count() >= 1500)
    std::cerr << "Shutdown took " << elapsed.count() << " ms" << std::endl;
  CHECK(elapsed.count() < 1500); // stopThread(2000) would force-kill otherwise
  client.close();
  temp.deleteRecursively();
}

} // namespace

int main() {
  std::cout << "===== Agent Control Server Tests =====" << std::endl;
  testAuthenticatedRequestAndDescriptorLifecycle();
  testShutdownWhileClientStopsReading();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
