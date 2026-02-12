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
  TcpRelay(const std::string &host = "127.0.0.1", int port = 5252);
  ~TcpRelay();

  /// Push a message to the send queue. Acquires mutex briefly.
  void pushMessage(const MidiEvent &event);

  /// Returns true if the relay is currently connected to the server.
  bool isConnected() const { return connected_.load(); }

  /// Set a callback for when connection state changes (called from relay
  /// thread).
  using ConnectionCallback = std::function<void(bool connected)>;
  void setConnectionCallback(ConnectionCallback cb);

private:
  void relayThread();
  bool tryConnect();
  void disconnect();
  bool sendMessage(const std::string &serialized);

  std::string host_;
  int port_;
  int socketFd_ = -1;

  std::atomic<bool> connected_{false};
  std::atomic<bool> running_{true};

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;

  std::thread thread_;

  ConnectionCallback connectionCallback_;
};

} // namespace fiddle
