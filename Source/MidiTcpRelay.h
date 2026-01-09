#pragma once

#include "midi_event.pb.h"
#include <google/protobuf/message.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <string>
#include <vector>

namespace fiddle {

/**
 * A non-blocking TCP relay for MIDI Protobuf messages.
 * Buffers messages from the audio thread and sends them over TCP in a
 * background thread.
 */
class MidiTcpRelay : public juce::Thread {
public:
  MidiTcpRelay() : juce::Thread("MidiTcpRelay") { startThread(); }

  ~MidiTcpRelay() override {
    signalThreadShouldExit();
    notify();
    stopThread(2000);
  }

  /**
   * Pushes a Protobuf message into the queue.
   * This is called from the audio thread, so it must be fast.
   */
  void pushMessage(const fiddle::MidiEvent &event) {
    const juce::ScopedLock sl(lock);

    // Serialize to a string buffer to move most work out of the lock/audio
    // thread if needed, but here we just store the proto and serialize in the
    // background thread.
    if (pendingMessages.size() < 1000) { // Safety cap
      pendingMessages.push_back(event);
      notify();
    }
  }

  bool isConnected() const { return socket.isConnected(); }

  void run() override {
    while (!threadShouldExit()) {
      if (!socket.isConnected()) {
        // Attempt to connect to localhost:5252 (default)
        DBG("MidiTcpRelay: Attempting to connect to 127.0.0.1:5252...");
        if (!socket.connect("127.0.0.1", 5252, 1000)) {
          DBG("MidiTcpRelay: Connection failed, retrying in 5s...");
          continue;
        }
      }

      std::vector<fiddle::MidiEvent> messagesToSend;
      {
        const juce::ScopedLock sl(lock);
        messagesToSend.swap(pendingMessages);
      }

      if (messagesToSend.empty()) {
        wait(5000); // Wait up to 5s for messages or timeout for heartbeat
        if (pendingMessages.empty() && socket.isConnected()) {
          fiddle::MidiEvent heartbeat;
          heartbeat.mutable_other()->set_description("Heartbeat");
          messagesToSend.push_back(heartbeat);
        } else {
          continue;
        }
      }

      for (const auto &msg : messagesToSend) {
        std::string binary;
        if (msg.SerializeToString(&binary)) {
          // Framing: 4-byte length prefix (Big Endian)
          uint32_t size = static_cast<uint32_t>(binary.size());
          uint32_t networkSize = juce::ByteOrder::swapIfLittleEndian(size);

          if (socket.write(&networkSize, 4) != 4) {
            std::cerr << "[MidiTcpRelay] Error writing length prefix"
                      << std::endl;
            socket.close();
            break;
          }

          if (socket.write(binary.data(), static_cast<int>(binary.size())) !=
              static_cast<int>(binary.size())) {
            std::cerr << "[MidiTcpRelay] Error writing binary payload"
                      << std::endl;
            socket.close();
            break;
          }
          std::cerr << "[MidiTcpRelay] Successfully sent payload of "
                    << binary.size() << " bytes" << std::endl;
        }
      }
    }

    socket.close();
  }

private:
  juce::StreamingSocket socket;
  juce::CriticalSection lock;
  std::vector<fiddle::MidiEvent> pendingMessages;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTcpRelay)
};

} // namespace fiddle
