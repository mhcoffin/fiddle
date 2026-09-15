#pragma once

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <utility>

namespace fiddle {

/// Authenticated loopback bridge used by the separately launched MCP adapter.
///
/// This is intentionally not an MCP implementation. Keeping the transport
/// boundary small lets the native app own live state and message-thread
/// dispatch while the official MCP SDK handles protocol compatibility.
class AgentControlServer final : public juce::Thread {
public:
  struct Response {
    bool ok = false;
    juce::var result;
    juce::String error;

    static Response success(juce::var value = {}) {
      return {true, std::move(value), {}};
    }
    static Response failure(juce::String message) {
      return {false, {}, std::move(message)};
    }
  };

  using Completion = std::function<void(Response)>;
  using Handler = std::function<void(const juce::String &method,
                                     const juce::var &params,
                                     Completion completion)>;

  AgentControlServer(juce::File descriptorFile, Handler handler,
                     int port = 0);
  ~AgentControlServer() override;

  void run() override;

  [[nodiscard]] int listeningPort() const noexcept {
    return listeningPort_.load();
  }
  [[nodiscard]] juce::String startupError() const;

private:
  struct PendingResponse;

  bool publishDescriptor(int port);
  void removeOwnedDescriptor();
  void handleConnection(std::unique_ptr<juce::StreamingSocket> socket);
  bool readLine(juce::StreamingSocket &socket, juce::String &line);
  static bool writeLine(juce::StreamingSocket &socket,
                        const juce::var &value);

  juce::File descriptorFile_;
  Handler handler_;
  int requestedPort_ = 0;
  juce::String token_;
  juce::StreamingSocket listener_;
  std::atomic<int> listeningPort_{0};
  mutable std::mutex stateMutex_;
  juce::String startupError_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgentControlServer)
};

} // namespace fiddle
