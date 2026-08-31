#pragma once

#include "../RealtimeSpscQueue.h"
#include "RealtimeMidiEvent.h"
#include "midi_event.pb.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
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
 * Audio-thread events use a bounded SPSC queue and are serialized by the
 * relay thread. Non-real-time callers may use pushMessage().
 */
class TcpRelay {
public:
  TcpRelay(const std::string &host = "127.0.0.1", int port = 5252,
           int initialDelayMs = 1000);
  ~TcpRelay();

  /// Start the relay thread. Call after setConnectionCallback().
  void start();

  /// Allow the already-running relay thread to connect. Audio-thread safe.
  void activate() noexcept {
    activated_.store(true, std::memory_order_release);
  }

  /// Enqueue a fixed-size event without allocation or locking.
  [[nodiscard]] bool
  pushRealtimeEvent(const RealtimeMidiEvent &event) noexcept;

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

  using ControlUpdateCallback = std::function<void()>;
  void setControlUpdateCallback(ControlUpdateCallback cb);

  /// Ask the relay thread to schedule a non-real-time controller update.
  void requestControlUpdate() noexcept {
    controlUpdateRequested_.store(true, std::memory_order_release);
  }

  [[nodiscard]] uint64_t droppedRealtimeEventCount() const noexcept {
    return droppedRealtimeEvents_.load(std::memory_order_relaxed);
  }

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
  std::atomic<bool> activated_{false};
  std::atomic<int> delayMs_;
  std::atomic<bool> latencyChanged_{false};
  std::atomic<bool> configChanged_{false};

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
  RealtimeSpscQueue<RealtimeMidiEvent, 8193> realtimeQueue_;
  std::atomic<uint64_t> droppedRealtimeEvents_{0};
  std::atomic<bool> controlUpdateRequested_{false};
  std::string configName_;
  std::string configVersion_;

  std::thread thread_;

  ConnectionCallback connectionCallback_;
  ControlUpdateCallback controlUpdateCallback_;
};

} // namespace fiddle
