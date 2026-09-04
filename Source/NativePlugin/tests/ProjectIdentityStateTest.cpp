#include "ProjectIdentityState.h"
#include "midi_event.pb.h"

#include <cstring>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "FAIL: " #condition << " at line " << __LINE__ << '\n';     \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

void append(std::vector<uint8_t> &bytes, const void *data, std::size_t size) {
  const auto *begin = static_cast<const uint8_t *>(data);
  bytes.insert(bytes.end(), begin, begin + size);
}

void appendString(std::vector<uint8_t> &bytes, const std::string &value) {
  const auto length = static_cast<int32_t>(value.size());
  append(bytes, &length, sizeof(length));
  append(bytes, value.data(), value.size());
}

fiddle::ProjectIdentityState decode(const std::vector<uint8_t> &bytes,
                                    bool &success) {
  std::size_t offset = 0;
  fiddle::ProjectIdentityState result;
  success = fiddle::readProjectIdentity(
      [&](void *destination, std::size_t size) {
        if (offset + size > bytes.size())
          return false;
        std::memcpy(destination, bytes.data() + offset, size);
        offset += size;
        return true;
      },
      result);
  return result;
}

void testRoundTrip() {
  const fiddle::ProjectIdentityState expected{"B2", "2026-09-02T17:00:00",
                                              "branch-uuid", "version-uuid"};
  std::vector<uint8_t> encoded;
  CHECK(fiddle::writeProjectIdentity(
      [&](const void *data, std::size_t size) {
        append(encoded, data, size);
        return true;
      },
      expected));

  bool success = false;
  const auto actual = decode(encoded, success);
  CHECK(success);
  CHECK(actual.branchName == expected.branchName);
  CHECK(actual.configVersion == expected.configVersion);
  CHECK(actual.branchId == expected.branchId);
  CHECK(actual.versionId == expected.versionId);
}

void testLegacyProject() {
  std::vector<uint8_t> encoded;
  appendString(encoded, "B2");
  appendString(encoded, "legacy-version");

  bool success = false;
  const auto actual = decode(encoded, success);
  CHECK(success);
  CHECK(actual.branchName == "B2");
  CHECK(actual.configVersion == "legacy-version");
  CHECK(actual.branchId.empty());
  CHECK(actual.versionId.empty());
}

void testIdentityProtocolRoundTrip() {
  fiddle::MidiEvent statusMessage;
  auto *status = statusMessage.mutable_config_status();
  status->set_config_name("B2");
  status->set_branch_id("branch-uuid");
  status->set_version_id("version-uuid");

  std::string encoded;
  CHECK(statusMessage.SerializeToString(&encoded));
  fiddle::MidiEvent receivedStatus;
  CHECK(receivedStatus.ParseFromString(encoded));
  CHECK(receivedStatus.config_status().branch_id() == "branch-uuid");
  CHECK(receivedStatus.config_status().version_id() == "version-uuid");

  fiddle::MidiEvent loadMessage;
  auto *load = loadMessage.mutable_load_config();
  load->set_config_path(receivedStatus.config_status().config_name());
  load->set_branch_id(receivedStatus.config_status().branch_id());
  load->set_version_id(receivedStatus.config_status().version_id());
  CHECK(loadMessage.SerializeToString(&encoded));
  fiddle::MidiEvent receivedLoad;
  CHECK(receivedLoad.ParseFromString(encoded));
  CHECK(receivedLoad.load_config().config_path() == "B2");
  CHECK(receivedLoad.load_config().branch_id() == "branch-uuid");
  CHECK(receivedLoad.load_config().version_id() == "version-uuid");
}

void testDoricoSaveHandshakeRoundTrip() {
  fiddle::MidiEvent request;
  request.mutable_save_config_request()->set_request_id(42);
  std::string encoded;
  CHECK(request.SerializeToString(&encoded));

  fiddle::MidiEvent receivedRequest;
  CHECK(receivedRequest.ParseFromString(encoded));
  CHECK(receivedRequest.has_save_config_request());
  CHECK(receivedRequest.save_config_request().request_id() == 42);

  fiddle::MidiEvent response;
  auto *saved = response.mutable_save_config_response();
  saved->set_request_id(receivedRequest.save_config_request().request_id());
  saved->set_success(true);
  saved->set_config_name("Orchestra");
  saved->set_branch_id("branch-after-save");
  saved->set_version_id("version-after-save");
  CHECK(response.SerializeToString(&encoded));

  fiddle::MidiEvent receivedResponse;
  CHECK(receivedResponse.ParseFromString(encoded));
  CHECK(receivedResponse.has_save_config_response());
  CHECK(receivedResponse.save_config_response().request_id() == 42);
  CHECK(receivedResponse.save_config_response().success());
  CHECK(receivedResponse.save_config_response().config_name() ==
        "Orchestra");
  CHECK(receivedResponse.save_config_response().branch_id() ==
        "branch-after-save");
  CHECK(receivedResponse.save_config_response().version_id() ==
        "version-after-save");
}

} // namespace

int main() {
  testRoundTrip();
  testLegacyProject();
  testIdentityProtocolRoundTrip();
  testDoricoSaveHandshakeRoundTrip();
  std::cout << "Failed: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
