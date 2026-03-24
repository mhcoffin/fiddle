#pragma once

#include "midi_event.pb.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace fiddle {

/**
 * TCP relay that sends protobuf-serialized MidiEvents to a remote server.
 * Uses std::thread and POSIX sockets (no JUCE dependency).
 *
 * Protocol: each message is sent as a 4-byte big-endian length prefix
 * followed by the serialized protobuf bytes.
 *
 * Thread safety:
 * - pushMessage() acquires a mutex briefly to enqueue. This is called from
 *   the audio thread. Since this plugin outputs silence (no audio synthesis),
 *   brief mutex contention won't cause audible artifacts. For a plugin that
 *   synthesizes audio, a lock-free queue would be preferable.
 * - setConnectionCallback() acquires the same mutex.
 * - The relay thread drains the queue under the same mutex.
 * - connected_ and running_ are std::atomic for lock-free status checks.
 */
class TcpRelay {
public:
  TcpRelay(const std::string &host = "127.0.0.1", int port = 5252,
           int initialDelayMs = 1000);
  ~TcpRelay();

  /// Start the relay thread. Call after setConnectionCallback().
  void start();

  /// Push a message to the send queue. Acquires mutex briefly.
  void pushMessage(const MidiEvent &event);

  /// Returns true if the relay is currently connected to the server.
  bool isConnected() const { return connected_.load(); }

  /// Returns the current playback delay in ms (lock-free, audio-thread safe).
  int getDelayMs() const { return delayMs_.load(std::memory_order_relaxed); }

  /// Returns true once when the delay value has changed. Resets the flag.
  bool consumeLatencyChanged() {
    return latencyChanged_.exchange(false, std::memory_order_relaxed);
  }

  /// Returns true once when the config name/version has changed. Resets the
  /// flag.
  bool consumeConfigChanged() {
    return configChanged_.exchange(false, std::memory_order_relaxed);
  }

  /// Returns the config name most recently received from the server.
  std::string getConfigName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configName_;
  }

  /// Returns the config version (ISO 8601 timestamp) from the server.
  std::string getConfigVersion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configVersion_;
  }

  /// Set a callback for when connection state changes (called from relay
  /// thread).
  using ConnectionCallback = std::function<void(bool connected)>;
  void setConnectionCallback(ConnectionCallback cb);

private:
  void relayThread();
  void receiveMessages();
  bool tryConnect();
  void disconnect();
  bool sendMessage(const std::string &serialized);

  std::string host_;
  int port_;
  int socketFd_ = -1;

  std::atomic<bool> connected_{false};
  std::atomic<bool> running_{true};
  std::atomic<int> delayMs_;
  std::atomic<bool> latencyChanged_{false};
  std::atomic<bool> configChanged_{false};

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
  std::string configName_;
  std::string configVersion_;

  std::thread thread_;

  ConnectionCallback connectionCallback_;
};

} // namespace fiddle
