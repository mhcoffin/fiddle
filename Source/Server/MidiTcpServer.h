#pragma once

#include "midi_event.pb.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

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

  /// Request that the current client connection be closed.
  void disconnectClient();

private:
  int port;
  juce::StreamingSocket listenerSocket;
  std::function<void(const fiddle::MidiEvent &)> messageCallback;
  std::function<void(juce::String)> rawActivityCallback;
  std::function<void(bool, juce::String)> connectionCallback;

  void handleConnection(std::unique_ptr<juce::StreamingSocket> clientSocket);

  std::atomic<bool> shouldDisconnect{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTcpServer)
};

} // namespace fiddle
