#include "AgentControlServer.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#if JUCE_WINDOWS
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fiddle {
namespace {

constexpr int kMaximumRequestBytes = 1024 * 1024;
constexpr auto kRequestTimeout = std::chrono::seconds(10);
constexpr auto kReadTimeout = std::chrono::seconds(5);

juce::var makeWireResponse(const juce::var &id,
                           const AgentControlServer::Response &response) {
  auto *object = new juce::DynamicObject();
  object->setProperty("id", id);
  object->setProperty("ok", response.ok);
  if (response.ok)
    object->setProperty("result", response.result);
  else
    object->setProperty("error", response.error);
  return juce::var(object);
}

} // namespace

struct AgentControlServer::PendingResponse {
  std::mutex mutex;
  std::condition_variable ready;
  bool completed = false;
  Response response;
};

AgentControlServer::AgentControlServer(juce::File descriptorFile,
                                       Handler handler, int port)
    : juce::Thread("AgentControlServer"),
      descriptorFile_(std::move(descriptorFile)),
      handler_(std::move(handler)), requestedPort_(port),
      token_(juce::Uuid().toString()) {}

AgentControlServer::~AgentControlServer() {
  listener_.close();
  stopThread(2000);
  removeOwnedDescriptor();
}

juce::String AgentControlServer::startupError() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return startupError_;
}

void AgentControlServer::run() {
  if (!listener_.createListener(requestedPort_, "127.0.0.1")) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    startupError_ = "Could not bind the agent control bridge to localhost";
    return;
  }

  const auto port = listener_.getBoundPort();
  if (!publishDescriptor(port)) {
    listener_.close();
    return;
  }
  listeningPort_.store(port);

  while (!threadShouldExit()) {
    std::unique_ptr<juce::StreamingSocket> socket(
        listener_.waitForNextConnection());
    if (socket)
      handleConnection(std::move(socket));
  }

  listeningPort_.store(0);
  listener_.close();
  removeOwnedDescriptor();
}

bool AgentControlServer::publishDescriptor(int port) {
  if (!descriptorFile_.getParentDirectory().createDirectory()) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    startupError_ = "Could not create the agent control data directory";
    return false;
  }

  auto *object = new juce::DynamicObject();
  object->setProperty("schemaVersion", 1);
  object->setProperty("transport", "tcp");
  object->setProperty("host", "127.0.0.1");
  object->setProperty("port", port);
  object->setProperty("token", token_);
#if JUCE_WINDOWS
  object->setProperty("pid", static_cast<juce::int64>(::_getpid()));
#else
  object->setProperty("pid", static_cast<juce::int64>(::getpid()));
#endif

#if JUCE_MAC || JUCE_LINUX
  // Establish restrictive permissions before writing the launch token. An
  // existing stale descriptor is tightened before it is replaced as well.
  descriptorFile_.create();
  ::chmod(descriptorFile_.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
#endif
  if (!descriptorFile_.replaceWithText(
          juce::JSON::toString(juce::var(object), true))) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    startupError_ = "Could not publish the agent control descriptor";
    return false;
  }

#if JUCE_MAC || JUCE_LINUX
  ::chmod(descriptorFile_.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
#endif
  return true;
}

void AgentControlServer::removeOwnedDescriptor() {
  if (!descriptorFile_.existsAsFile())
    return;
  const auto descriptor = juce::JSON::parse(descriptorFile_);
  if (descriptor["token"].toString() == token_)
    descriptorFile_.deleteFile();
}

bool AgentControlServer::readLine(juce::StreamingSocket &socket,
                                  juce::String &line) {
  juce::MemoryBlock bytes;
  char buffer[4096];
  const auto deadline = std::chrono::steady_clock::now() + kReadTimeout;
  while (!threadShouldExit() &&
         std::chrono::steady_clock::now() < deadline &&
         bytes.getSize() <= kMaximumRequestBytes) {
    const int ready = socket.waitUntilReady(true, 200);
    if (ready < 0)
      return false;
    if (ready == 0)
      continue;
    const int count = socket.read(buffer, sizeof(buffer), false);
    if (count <= 0)
      return false;
    const auto *newline = static_cast<const char *>(
        std::memchr(buffer, '\n', static_cast<size_t>(count)));
    const auto useful = newline ? static_cast<size_t>(newline - buffer)
                                : static_cast<size_t>(count);
    bytes.append(buffer, useful);
    if (bytes.getSize() > kMaximumRequestBytes)
      return false;
    if (newline) {
      line = juce::String::fromUTF8(static_cast<const char *>(bytes.getData()),
                                    static_cast<int>(bytes.getSize()));
      return true;
    }
  }
  return false;
}

bool AgentControlServer::writeLine(juce::StreamingSocket &socket,
                                   const juce::var &value) {
  const auto text = juce::JSON::toString(value, true) + "\n";
  return socket.write(text.toRawUTF8(), text.getNumBytesAsUTF8()) ==
         text.getNumBytesAsUTF8();
}

void AgentControlServer::handleConnection(
    std::unique_ptr<juce::StreamingSocket> socket) {
  juce::String line;
  if (!readLine(*socket, line))
    return;

  const auto request = juce::JSON::parse(line);
  const auto id = request["id"];
  if (!request.isObject()) {
    writeLine(*socket, makeWireResponse(
                           id, Response::failure("Malformed request")));
    return;
  }
  if (request["token"].toString() != token_) {
    writeLine(*socket, makeWireResponse(
                           id, Response::failure("Authentication failed")));
    return;
  }
  const auto method = request["method"].toString();
  if (method.isEmpty()) {
    writeLine(*socket,
              makeWireResponse(id, Response::failure("Missing method")));
    return;
  }
  if (!handler_) {
    writeLine(*socket, makeWireResponse(
                           id, Response::failure("Control bridge unavailable")));
    return;
  }

  auto pending = std::make_shared<PendingResponse>();
  try {
    handler_(method, request["params"], [pending](Response response) {
      std::lock_guard<std::mutex> lock(pending->mutex);
      if (pending->completed)
        return;
      pending->response = std::move(response);
      pending->completed = true;
      pending->ready.notify_one();
    });
  } catch (const std::exception &error) {
    writeLine(*socket,
              makeWireResponse(id, Response::failure(error.what())));
    return;
  }

  std::unique_lock<std::mutex> lock(pending->mutex);
  const auto deadline = std::chrono::steady_clock::now() + kRequestTimeout;
  while (!pending->completed && !threadShouldExit() &&
         std::chrono::steady_clock::now() < deadline)
    pending->ready.wait_for(lock, std::chrono::milliseconds(100));
  if (!pending->completed) {
    pending->completed = true;
    lock.unlock();
    if (!threadShouldExit())
      writeLine(*socket,
                makeWireResponse(id, Response::failure("Fiddle timed out")));
    return;
  }
  const auto response = std::move(pending->response);
  lock.unlock();
  writeLine(*socket, makeWireResponse(id, response));
}

} // namespace fiddle
