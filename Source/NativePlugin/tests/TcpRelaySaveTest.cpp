#include "TcpRelay.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
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

void testBackgroundDiagnostics() {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
  CHECK(::listen(listener, 1) == 0);
  socklen_t size = sizeof(address);
  CHECK(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) == 0);
  std::atomic<bool> gotNote{false}, gotDiagnostics{false}, background{false};
  std::atomic<int> reports{0};
  std::thread server([&] {
    pollfd ready{listener, POLLIN, 0};
    if (::poll(&ready, 1, 3000) <= 0) return;
    const int client = ::accept(listener, nullptr, nullptr);
    if (client < 0) return;
    timeval timeout{3, 0};
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    for (int i = 0; i < 2; ++i) {
      fiddle::MidiEvent event;
      if (!readEvent(client, event)) break;
      if (event.has_note_on()) gotNote.store(true);
      if (event.has_audio_diagnostics())
        gotDiagnostics.store(event.audio_diagnostics().underruns() == 7 &&
                             event.audio_diagnostics().sample_rate() == 48000 &&
                             event.audio_diagnostics().safety_mute_episodes() == 3 &&
                             event.audio_diagnostics().safety_mute_version() == 1);
    }
    ::close(client);
  });
  {
    fiddle::TcpRelay relay("127.0.0.1", ntohs(address.sin_port), 1000);
    const auto caller = std::this_thread::get_id();
    relay.setDiagnosticsProvider([&] {
      background.store(std::this_thread::get_id() != caller);
      ++reports;
      fiddle::MidiEvent event;
      event.mutable_audio_diagnostics()->set_underruns(7);
      event.mutable_audio_diagnostics()->set_sample_rate(48000);
      event.mutable_audio_diagnostics()->set_safety_mute_episodes(3);
      event.mutable_audio_diagnostics()->set_safety_mute_version(1);
      return event;
    });
    fiddle::MidiEvent note;
    note.mutable_note_on()->set_note_number(60);
    relay.pushMessage(note);
    relay.start();
    relay.activate();
    server.join();
  }
  ::close(listener);
  CHECK(gotNote && gotDiagnostics && background && reports == 1);
}

} // namespace

int main() {
  testCorrelatedSaveResponse();
  testBackgroundDiagnostics();
  std::cout << "Failed: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
