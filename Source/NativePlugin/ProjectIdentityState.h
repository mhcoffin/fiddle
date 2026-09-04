#pragma once

#include <cstdint>
#include <string>

namespace fiddle {

struct ProjectIdentityState {
  std::string branchName;
  std::string configVersion;
  std::string branchId;
  std::string versionId;
};

inline constexpr int32_t kMaxProjectIdentityFieldBytes = 4096;

template <typename Write>
bool writeProjectIdentity(Write &&write, const ProjectIdentityState &identity) {
  const auto writeString = [&](const std::string &value) {
    const auto length = static_cast<int32_t>(value.size());
    return length < kMaxProjectIdentityFieldBytes &&
           write(&length, sizeof(length)) &&
           (length == 0 || write(value.data(), length));
  };

  return writeString(identity.branchName) &&
         writeString(identity.configVersion) &&
         writeString(identity.branchId) && writeString(identity.versionId);
}

/// Read the identity tail appended after Fiddle's channel program state.
/// Branch/version UUIDs are optional because older Dorico projects end after
/// branchName and configVersion.
template <typename Read>
bool readProjectIdentity(Read &&read, ProjectIdentityState &identity) {
  identity = {};
  const auto readString = [&](std::string &value) {
    int32_t length = 0;
    if (!read(&length, sizeof(length)) || length < 0 ||
        length >= kMaxProjectIdentityFieldBytes)
      return false;
    if (length == 0)
      return true;
    value.resize(static_cast<std::size_t>(length));
    return read(value.data(), static_cast<std::size_t>(length));
  };

  if (!readString(identity.branchName))
    return false;
  if (!readString(identity.configVersion))
    return true;
  if (!readString(identity.branchId))
    return true;
  (void)readString(identity.versionId);
  return true;
}

} // namespace fiddle
