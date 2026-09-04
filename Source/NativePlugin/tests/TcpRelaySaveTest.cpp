#include "TcpRelay.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "FAIL: " #condition << " at line " << __LINE__ << '\n';     \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

bool readExactly(int socket, void *destination, std::size_t size) {
  auto *bytes = static_cast<uint8_t *>(destination);
  std::size_t received = 0;
  while (received < size) {
    const auto result =
        ::recv(socket, bytes + received, size - received, MSG_WAITALL);
    if (result <= 0)
      return false;
    received += static_cast<std::size_t>(result);
  }
  return true;
}

bool readEvent(int socket, fiddle::MidiEvent &event) {
  uint8_t header[4]{};
  if (!readExactly(socket, header, sizeof(header)))
    return false;
  const uint32_t size = (uint32_t(header[0]) << 24) |
                        (uint32_t(header[1]) << 16) |
                        (uint32_t(header[2]) << 8) | uint32_t(header[3]);
  std::string payload(size, '\0');
  return readExactly(socket, payload.data(), payload.size()) &&
         event.ParseFromString(payload);
}

bool sendEvent(int socket, const fiddle::MidiEvent &event) {
  std::string payload;
  if (!event.SerializeToString(&payload))
    return false;
  const uint32_t size = static_cast<uint32_t>(payload.size());
  const uint8_t header[4]{static_cast<uint8_t>(size >> 24),
                          static_cast<uint8_t>(size >> 16),
                          static_cast<uint8_t>(size >> 8),
                          static_cast<uint8_t>(size)};
  return ::send(socket, header, sizeof(header), MSG_NOSIGNAL) ==
             sizeof(header) &&
         ::send(socket, payload.data(), payload.size(), MSG_NOSIGNAL) ==
             static_cast<ssize_t>(payload.size());
}

void testCorrelatedSaveResponse() {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0)
    return;

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  CHECK(::bind(listener, reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) == 0);
  CHECK(::listen(listener, 1) == 0);
  socklen_t addressSize = sizeof(address);
  CHECK(::getsockname(listener, reinterpret_cast<sockaddr *>(&address),
                      &addressSize) == 0);
  const int port = ntohs(address.sin_port);

  std::thread server([listener] {
    const int client = ::accept(listener, nullptr, nullptr);
    if (client >= 0) {
      fiddle::MidiEvent request;
      if (readEvent(client, request) && request.has_save_config_request()) {
        fiddle::MidiEvent response;
        auto *saved = response.mutable_save_config_response();
        saved->set_request_id(request.save_config_request().request_id());
        saved->set_success(true);
        saved->set_config_name("B2");
        saved->set_branch_id("branch-2");
        saved->set_version_id("version-after-dorico-save");
        sendEvent(client, response);
      }
      ::close(client);
    }
    ::close(listener);
  });

  {
    fiddle::TcpRelay relay("127.0.0.1", port, 1000);
    relay.start();
    relay.activate();
    const auto result =
        relay.requestSaveSnapshot(std::chrono::milliseconds(2000));
    CHECK(result.success);
    CHECK(result.configName == "B2");
    CHECK(result.branchId == "branch-2");
    CHECK(result.versionId == "version-after-dorico-save");
  }

  server.join();
}

} // namespace

int main() {
  testCorrelatedSaveResponse();
  std::cout << "Failed: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
