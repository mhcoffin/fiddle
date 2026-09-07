#pragma once

#include "midi_event.pb.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <mutex>

namespace fiddle {

/**
 * A TCP server that listens for MIDI Protobuf messages.
 */
class MidiTcpServer : public juce::Thread {
public:
  MidiTcpServer(int port = 5252);
  ~MidiTcpServer() override;

  void run() override;

  /**
   * Callback for when a new MIDI event is received.
   */
  void
  onMessageReceived(std::function<void(const fiddle::MidiEvent &)> callback);

  void onRawActivity(std::function<void(juce::String)> callback);

  void onConnectionChanged(std::function<void(bool, juce::String)> callback);

  /// Called once per primary-client session when another client attempts to
  /// connect. The additional connection is rejected; the established client
  /// remains active.
  void onAdditionalConnectionAttempt(
      std::function<void(juce::String)> callback);

  /// Request that the current client connection be closed.
  void disconnectClient();

  /// Send a protobuf message to the connected client (server → plugin).
  /// Thread-safe. Returns false if no client connected or write fails.
  bool sendToClient(const fiddle::MidiEvent &msg);

  /// Cache a config_status message (called from message thread).
  /// The cached bytes are pushed to new clients immediately on connect.
  void setCachedConfigStatus(const fiddle::MidiEvent &msg);

private:
  int port;
  juce::StreamingSocket listenerSocket;
  std::function<void(const fiddle::MidiEvent &)> messageCallback;
  std::function<void(juce::String)> rawActivityCallback;
  std::function<void(bool, juce::String)> connectionCallback;
  std::function<void(juce::String)> additionalConnectionCallback;

  void handleConnection(std::unique_ptr<juce::StreamingSocket> clientSocket);
  void rejectPendingAdditionalConnections();

  std::atomic<bool> shouldDisconnect{false};
  bool additionalConnectionReported_{false};
  std::mutex clientMutex_;
  juce::StreamingSocket *currentClient_{nullptr};
  std::string cachedConfigStatus_; // guarded by clientMutex_

  /// Send the cached config_status bytes to the current client.
  /// Must be called with clientMutex_ NOT held (acquires it internally).
  void sendCachedConfigStatus();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTcpServer)
};

} // namespace fiddle
