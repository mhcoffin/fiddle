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
    : host_(host), port_(port) {
  thread_ = std::thread(&TcpRelay::relayThread, this);
}

TcpRelay::~TcpRelay() {
  running_ = false;
  cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
  disconnect();
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

      if (queue_.empty()) {
        // No messages to send — check if the server is still alive.
        // A non-blocking recv() returns 0 on clean close, or -1 with
        // an error (other than EAGAIN/EWOULDBLOCK) on broken connection.
        if (connected_ && socketFd_ >= 0) {
          char probe;
          ssize_t n = ::recv(socketFd_, &probe, 1, MSG_DONTWAIT | MSG_PEEK);
          if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Server closed the connection
            lock.unlock();
            disconnect();
            connected_ = false;
            ConnectionCallback cb;
            {
              std::lock_guard<std::mutex> cbLock(mutex_);
              cb = connectionCallback_;
            }
            if (cb)
              cb(false);
          }
        }
        continue;
      }

      msg = std::move(queue_.front());
      queue_.pop_front();
    }

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
