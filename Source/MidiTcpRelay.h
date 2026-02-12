#pragma once

#include "midi_event.pb.h"
#include <atomic>
#include <google/protobuf/message.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
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
    stopThread(5000); // 5s timeout for safer shutdown
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

  bool isConnected() const { return connected.load(); }

  void run() override {
    uint32_t lastConnectAttempt = 0;

    while (!threadShouldExit()) {
      bool isNowConnected = false;
      {
        const juce::ScopedLock sl(lock);
        isNowConnected = (socket != nullptr && connected.load());
      }

      if (!isNowConnected) {
        auto now = juce::Time::getMillisecondCounter();
        if (now - lastConnectAttempt > 5000) {
          lastConnectAttempt = now;

          // Re-create socket to ensure fresh state
          {
            const juce::ScopedLock sl(lock);
            socket = std::make_unique<juce::StreamingSocket>();
            connected.store(false);
          }

          if (socket->connect("127.0.0.1", 5252, 500)) {
            connected.store(true);
          }
        }
      }

      std::vector<fiddle::MidiEvent> messagesToSend;
      {
        const juce::ScopedLock sl(lock);
        if (!pendingMessages.empty()) {
          messagesToSend.swap(pendingMessages);
        }
      }

      if (messagesToSend.empty()) {
        if (connected.load()) {
          // Heartbeat check (Disabled to reduce noise)
          wait(5000);
          const juce::ScopedLock sl(lock);
          // if (pendingMessages.empty() && connected.load()) {
          //   fiddle::MidiEvent heartbeat;
          //   heartbeat.mutable_other()->set_description("Heartbeat");
          //   messagesToSend.push_back(heartbeat);
          // }
        } else {
          wait(500); // Shorter wait when disconnected to keep loop responsive
          continue;
        }
      }

      if (messagesToSend.empty())
        continue;

      for (const auto &msg : messagesToSend) {
        if (threadShouldExit())
          break;

        std::string binary;
        if (msg.SerializeToString(&binary)) {
          uint32_t size = static_cast<uint32_t>(binary.size());
          uint32_t networkSize = juce::ByteOrder::swapIfLittleEndian(size);

          bool success = false;
          if (connected.load()) {
            if (socket->waitUntilReady(false, 100) > 0) {
              if (socket->write(&networkSize, 4) == 4) {
                if (socket->write(binary.data(), (int)binary.size()) ==
                    (int)binary.size()) {
                  success = true;
                }
              }
            }
          }

          if (!success) {
            const juce::ScopedLock sl(lock);
            if (socket != nullptr)
              socket->close();
            connected.store(false);
            break;
          }
        }
      }
    }

    const juce::ScopedLock sl(lock);
    if (socket != nullptr)
      socket->close();
    connected.store(false);
  }

private:
  std::unique_ptr<juce::StreamingSocket> socket;
  juce::CriticalSection lock;
  std::vector<fiddle::MidiEvent> pendingMessages;
  std::atomic<bool> connected{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTcpRelay)
};

} // namespace fiddle
