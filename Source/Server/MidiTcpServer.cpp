#include "MidiTcpServer.h"
#include <juce_core/juce_core.h>

namespace fiddle {

MidiTcpServer::MidiTcpServer(int port)
    : juce::Thread("MidiTcpServer"), port(port) {
  // Thread is NOT started here — MainComponent::MainComponent() will call
  // startThread() after all callbacks (onMessageReceived, onConnectionChanged,
  // onRawActivity) are registered. Starting the thread here would create a
  // race condition: the server could accept a connection before callbacks are
  // set, causing handleConnection to run with null callbacks and immediately
  // drop the client.
}

MidiTcpServer::~MidiTcpServer() {
  // Close both sockets to unblock wherever the thread is stuck:
  // - listenerSocket.close() unblocks waitForNextConnection()
  // - currentClient_ close unblocks clientSocket->read() in handleConnection()
  listenerSocket.close();
  {
    std::lock_guard<std::mutex> lock(clientMutex_);
    if (currentClient_)
      currentClient_->close();
  }
  stopThread(2000);
}

void MidiTcpServer::onMessageReceived(
    std::function<void(const fiddle::MidiEvent &)> callback) {
  messageCallback = callback;
}

void MidiTcpServer::onRawActivity(std::function<void(juce::String)> callback) {
  rawActivityCallback = callback;
}

void MidiTcpServer::onConnectionChanged(
    std::function<void(bool, juce::String)> callback) {
  connectionCallback = callback;
}

void MidiTcpServer::disconnectClient() { shouldDisconnect.store(true); }

bool MidiTcpServer::sendToClient(const fiddle::MidiEvent &msg) {
  std::lock_guard<std::mutex> lock(clientMutex_);
  if (!currentClient_ || !currentClient_->isConnected())
    return false;

  std::string binary;
  if (!msg.SerializeToString(&binary))
    return false;

  uint32_t size = static_cast<uint32_t>(binary.size());
  uint32_t networkSize = juce::ByteOrder::swapIfLittleEndian(size);

  if (currentClient_->write(&networkSize, 4) != 4)
    return false;
  if (currentClient_->write(binary.data(), (int)binary.size()) !=
      (int)binary.size())
    return false;

  return true;
}

void MidiTcpServer::run() {
  if (!listenerSocket.createListener(port)) {
    DBG("MidiTcpServer: Failed to create listener on port " << port);
    return;
  }

  DBG("MidiTcpServer: Listening on port " << port);

  while (!threadShouldExit()) {
    auto *client = listenerSocket.waitForNextConnection();
    if (client != nullptr) {
      // Register client BEFORE firing the callback so sendToClient() works
      // from within the connectionCallback (e.g. to push config status).
      {
        std::lock_guard<std::mutex> lock(clientMutex_);
        currentClient_ = client;
      }
      // Push cached config_status immediately from the TCP thread.
      // This avoids waiting for the message thread (which may be busy
      // loading plugins) to run safeCallAsync.
      sendCachedConfigStatus();
      if (connectionCallback) {
        connectionCallback(true, client->getHostName());
      }
      handleConnection(std::unique_ptr<juce::StreamingSocket>(client));
      if (connectionCallback) {
        connectionCallback(false, "");
      }
    }
  }

  listenerSocket.close();
}

void MidiTcpServer::handleConnection(
    std::unique_ptr<juce::StreamingSocket> clientSocket) {
  DBG("MidiTcpServer: Client connected from " << clientSocket->getHostName());

  // Note: currentClient_ is already set by run() before this is called.

  while (!threadShouldExit() && clientSocket->isConnected() &&
         !shouldDisconnect.load()) {
    // Read 4-byte length prefix
    uint32_t networkSize = 0;
    int bytesRead = clientSocket->read(&networkSize, 4, true);

    if (bytesRead != 4) {
      DBG("MidiTcpServer: Client disconnected or error reading header");
      break;
    }

    if (rawActivityCallback) {
      rawActivityCallback("Header (4 bytes) read");
    }

    uint32_t size = juce::ByteOrder::swapIfLittleEndian(networkSize);
    if (size > 1024 * 1024) { // 1MB sanity check
      DBG("MidiTcpServer: Invalid message size: " << (int)size);
      break;
    }

    // Read payload
    std::vector<char> buffer(size);
    bytesRead = clientSocket->read(buffer.data(), (int)size, true);

    if (bytesRead != (int)size) {
      DBG("MidiTcpServer: Error reading payload");
      break;
    }

    if (rawActivityCallback) {
      rawActivityCallback("Payload (" + juce::String((int)size) +
                          " bytes) read");
    }

    // Decode Protobuf
    fiddle::MidiEvent event;
    if (event.ParseFromArray(buffer.data(), (int)size)) {
      std::cerr << "[MidiTcpServer] Parsed Protobuf event of type "
                << event.event_case() << std::endl;
      if (messageCallback) {
        messageCallback(event);
      }
    } else {
      std::cerr << "[MidiTcpServer] Error: Failed to parse Protobuf message"
                << std::endl;
    }
  }

  // Unregister client
  {
    std::lock_guard<std::mutex> lock(clientMutex_);
    currentClient_ = nullptr;
  }
  shouldDisconnect.store(false);
  DBG("MidiTcpServer: Connection closed");
}

} // namespace fiddle

void fiddle::MidiTcpServer::setCachedConfigStatus(
    const fiddle::MidiEvent &msg) {
  std::string binary;
  if (!msg.SerializeToString(&binary))
    return;
  std::lock_guard<std::mutex> lock(clientMutex_);
  cachedConfigStatus_ = std::move(binary);
}

void fiddle::MidiTcpServer::sendCachedConfigStatus() {
  std::lock_guard<std::mutex> lock(clientMutex_);
  if (!currentClient_ || cachedConfigStatus_.empty())
    return;

  uint32_t size = static_cast<uint32_t>(cachedConfigStatus_.size());
  uint32_t networkSize = juce::ByteOrder::swapIfLittleEndian(size);

  currentClient_->write(&networkSize, 4);
  currentClient_->write(cachedConfigStatus_.data(),
                        (int)cachedConfigStatus_.size());
}
