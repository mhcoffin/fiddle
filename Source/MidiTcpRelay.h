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

    if (pendingMessages.size() < 1000) { // Safety cap
      pendingMessages.push_back(event);
      notify();
    }
  }

  bool isConnected() const { return connected.load(); }

  /// Store the config path to announce on connect
  void setConfigPath(const juce::String &path) {
    const juce::ScopedLock sl(lock);
    announcedConfigPath = path;
  }

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

            // Announce our config path on connect
            juce::String pathToAnnounce;
            {
              const juce::ScopedLock sl(lock);
              pathToAnnounce = announcedConfigPath;
            }
            fiddle::MidiEvent hello;
            hello.set_timestamp_samples(0);
            hello.mutable_load_config()->set_config_path(
                pathToAnnounce.toStdString());
            pushMessage(hello);
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
          // Wait briefly, then probe the socket to detect dead connections.
          // TCP won't tell us the server closed unless we try to read/write.
          wait(2000);

          // Probe: attempt a non-blocking read. If server closed, read
          // returns 0 or -1.
          if (socket != nullptr && connected.load()) {
            char probe;
            int ready = socket->waitUntilReady(true, 0); // readable?
            if (ready > 0) {
              int r = socket->read(&probe, 1, false);
              if (r <= 0) {
                // Server closed the connection
                const juce::ScopedLock sl(lock);
                if (socket != nullptr)
                  socket->close();
                connected.store(false);
              }
            } else if (ready < 0) {
              // Socket error
              const juce::ScopedLock sl(lock);
              if (socket != nullptr)
                socket->close();
              connected.store(false);
            }
          }
          continue;
        } else {
          wait(500); // Shorter wait when disconnected to keep loop responsive
          continue;
        }
      }

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
  juce::String announcedConfigPath;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiTcpRelay)
};

} // namespace fiddle
