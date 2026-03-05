#include "TcpRelay.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fiddle {

TcpRelay::TcpRelay(const std::string &host, int port)
    : host_(host), port_(port) {}

void TcpRelay::start() { thread_ = std::thread(&TcpRelay::relayThread, this); }

TcpRelay::~TcpRelay() {
  running_ = false;
  // Close the socket first to unblock any blocking recv() in the relay thread.
  // cv_.notify_all() only wakes threads waiting on the condition variable,
  // not threads blocked in syscalls like recv().
  disconnect();
  cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void TcpRelay::pushMessage(const MidiEvent &event) {
  std::string serialized;
  if (!event.SerializeToString(&serialized))
    return;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() < 4096) {
      queue_.push_back(std::move(serialized));
    }
  }
  cv_.notify_one();
}

void TcpRelay::setConnectionCallback(ConnectionCallback cb) {
  std::lock_guard<std::mutex> lock(mutex_);
  connectionCallback_ = std::move(cb);
}

void TcpRelay::relayThread() {
  while (running_) {
    // Try to connect if not connected
    if (!connected_) {
      if (tryConnect()) {
        connected_ = true;
        // Copy callback under lock, then invoke OUTSIDE the lock
        // to avoid deadlock (callback may call pushMessage which locks mutex_)
        ConnectionCallback cb;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          cb = connectionCallback_;
        }
        if (cb)
          cb(true);
      } else {
        // Wait before retry
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(1),
                     [this] { return !running_.load(); });
        continue;
      }
    }

    // Drain the queue
    std::string msg;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(100),
                   [this] { return !queue_.empty() || !running_.load(); });

      if (!running_)
        break;

      if (!queue_.empty()) {
        msg = std::move(queue_.front());
        queue_.pop_front();
      }
    }

    // Always check for incoming server→plugin messages (non-blocking).
    // This must run even when the outgoing queue has data, otherwise
    // a flood of outgoing MIDI events starves config_status reception.
    if (connected_ && socketFd_ >= 0) {
      receiveMessages();
    }

    if (msg.empty())
      continue;

    if (!sendMessage(msg)) {
      disconnect();
      connected_ = false;
      // Copy callback under lock, invoke outside
      ConnectionCallback cb;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = connectionCallback_;
      }
      if (cb)
        cb(false);
    }
  }
}

bool TcpRelay::tryConnect() {
  socketFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socketFd_ < 0)
    return false;

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
    ::close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  // Set a short connect timeout using non-blocking connect
  int flags = ::fcntl(socketFd_, F_GETFL, 0);
  ::fcntl(socketFd_, F_SETFL, flags | O_NONBLOCK);

  int result = ::connect(socketFd_, (struct sockaddr *)&addr, sizeof(addr));
  if (result < 0 && errno != EINPROGRESS) {
    ::close(socketFd_);
    socketFd_ = -1;
    return false;
  }

  if (result < 0) {
    // Wait for connection with timeout
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(socketFd_, &writefds);
    struct timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    result = ::select(socketFd_ + 1, nullptr, &writefds, nullptr, &tv);
    if (result <= 0) {
      ::close(socketFd_);
      socketFd_ = -1;
      return false;
    }

    // Check for connection error
    int optval = 0;
    socklen_t optlen = sizeof(optval);
    if (::getsockopt(socketFd_, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0 ||
        optval != 0) {
      ::close(socketFd_);
      socketFd_ = -1;
      return false;
    }
  }

  // Set back to blocking mode for sends
  ::fcntl(socketFd_, F_SETFL, flags);

  // Disable Nagle's algorithm for low latency
  int nodelay = 1;
  ::setsockopt(socketFd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

  return true;
}

void TcpRelay::disconnect() {
  if (socketFd_ >= 0) {
    ::close(socketFd_);
    socketFd_ = -1;
  }
}

void TcpRelay::receiveMessages() {
  // Try to read all available server→plugin messages.
  // Non-blocking: returns immediately if nothing to read.
  while (connected_ && socketFd_ >= 0) {
    // Peek to see if data is available (non-blocking)
    char peek;
    ssize_t n = ::recv(socketFd_, &peek, 1, MSG_DONTWAIT | MSG_PEEK);
    if (n == 0) {
      // Server closed the connection
      disconnect();
      connected_ = false;
      ConnectionCallback cb;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = connectionCallback_;
      }
      if (cb)
        cb(false);
      return;
    }
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return; // No data available, all good
      // Error — connection broken
      disconnect();
      connected_ = false;
      ConnectionCallback cb;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = connectionCallback_;
      }
      if (cb)
        cb(false);
      return;
    }

    // Read 4-byte big-endian length prefix (blocking, but we know data exists)
    uint8_t header[4];
    ssize_t r = ::recv(socketFd_, header, 4, MSG_WAITALL);
    if (r != 4) {
      disconnect();
      connected_ = false;
      ConnectionCallback cb;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = connectionCallback_;
      }
      if (cb)
        cb(false);
      return;
    }

    uint32_t size = (uint32_t(header[0]) << 24) | (uint32_t(header[1]) << 16) |
                    (uint32_t(header[2]) << 8) | uint32_t(header[3]);

    if (size > 1024 * 1024) {
      // Invalid size, disconnect
      disconnect();
      connected_ = false;
      return;
    }

    // Read payload
    std::string payload(size, '\0');
    size_t totalRead = 0;
    while (totalRead < size) {
      ssize_t bytesRead =
          ::recv(socketFd_, &payload[totalRead], size - totalRead, 0);
      if (bytesRead <= 0) {
        disconnect();
        connected_ = false;
        ConnectionCallback cb;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          cb = connectionCallback_;
        }
        if (cb)
          cb(false);
        return;
      }
      totalRead += bytesRead;
    }

    // Parse protobuf
    MidiEvent event;
    if (event.ParseFromString(payload)) {
      if (event.has_config_status()) {
        auto &cs = event.config_status();
        int newDelay = cs.delay_ms();
        int oldDelay = delayMs_.exchange(newDelay, std::memory_order_relaxed);
        if (oldDelay != newDelay) {
          latencyChanged_.store(true, std::memory_order_relaxed);
        }
        {
          std::lock_guard<std::mutex> lock(mutex_);
          configName_ = cs.config_name();
          configVersion_ = cs.config_version();
        }
        configChanged_.store(true, std::memory_order_relaxed);
      }
    }
  }
}

bool TcpRelay::sendMessage(const std::string &serialized) {
  if (socketFd_ < 0)
    return false;

  // 4-byte big-endian length prefix
  uint32_t len = static_cast<uint32_t>(serialized.size());
  uint8_t header[4] = {
      static_cast<uint8_t>((len >> 24) & 0xFF),
      static_cast<uint8_t>((len >> 16) & 0xFF),
      static_cast<uint8_t>((len >> 8) & 0xFF),
      static_cast<uint8_t>(len & 0xFF),
  };

  // Send length prefix
  ssize_t sent = ::send(socketFd_, header, 4, MSG_NOSIGNAL);
  if (sent != 4)
    return false;

  // Send payload
  size_t totalSent = 0;
  while (totalSent < serialized.size()) {
    sent = ::send(socketFd_, serialized.data() + totalSent,
                  serialized.size() - totalSent, MSG_NOSIGNAL);
    if (sent <= 0)
      return false;
    totalSent += sent;
  }

  return true;
}

} // namespace fiddle
