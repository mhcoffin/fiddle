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

} // namespace

int main() {
  std::cout << "===== Agent Control Server Tests =====" << std::endl;
  testAuthenticatedRequestAndDescriptorLifecycle();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
