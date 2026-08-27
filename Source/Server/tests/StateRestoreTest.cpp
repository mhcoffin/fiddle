#include "../StateBlobTypes.h"

#include <cstring>
#include <iostream>
#include <string>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition) {                                                           \
      ++passed;                                                                \
    } else {                                                                   \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                   \
                << "]: " #condition << std::endl;                            \
    }                                                                          \
  } while (false)

template <typename T> void append(juce::MemoryBlock &blob, T value) {
  blob.append(&value, sizeof(value));
}

void appendString(juce::MemoryBlock &blob, const std::string &value) {
  append<uint32_t>(blob, static_cast<uint32_t>(value.size()));
  blob.append(value.data(), value.size());
}

juce::MemoryBlock makeBlobWithActive(bool active, bool includeActive = true) {
  juce::MemoryBlock blob;
  append<uint32_t>(blob, fiddle::kStateBlobMagic);
  append<uint32_t>(blob, fiddle::kStateBlobVersion);
  append<uint32_t>(blob, 0);
  appendString(blob, "Regression Test");
  appendString(blob, "1");
  append<uint8_t>(blob, 0);
  appendString(blob, "state-hash");
  append<uint32_t>(blob, 0);
  append<uint32_t>(blob, 1);

  const std::string activeProperty =
      includeActive
          ? std::string{"\"active\":"} + (active ? "true," : "false,")
          : "";
  const std::string json =
      std::string{"{\"id\":\"strip-1\",\"library\":\"Test\",\"family\":"
                  "\"Strings\",\"isSolo\":true,"} +
      activeProperty +
      "\"inputPort\":0,\"inputChannel\":1,\"pluginUid\":0,"
      "\"gainDb\":0.0,\"expressionMap\":\"\",\"luaPlugins\":[]}";
  appendString(blob, json);
  append<uint32_t>(blob, 0);

  auto totalSize = static_cast<uint32_t>(blob.getSize() - 12);
  std::memcpy(static_cast<uint8_t *>(blob.getData()) + 8, &totalSize,
              sizeof(totalSize));
  return blob;
}

void testActiveStateRoundTripsThroughDeserialization() {
  auto inactiveBlob = makeBlobWithActive(false);
  auto inactive = fiddle::deserializeStateBlob(inactiveBlob.getData(),
                                                inactiveBlob.getSize());
  CHECK(inactive.has_value());
  CHECK(inactive && inactive->strips.size() == 1);
  CHECK(inactive && !inactive->strips.front().active);

  auto activeBlob = makeBlobWithActive(true);
  auto active =
      fiddle::deserializeStateBlob(activeBlob.getData(), activeBlob.getSize());
  CHECK(active.has_value());
  CHECK(active && active->strips.size() == 1);
  CHECK(active && active->strips.front().active);

  auto legacyBlob = makeBlobWithActive(false, false);
  auto legacy =
      fiddle::deserializeStateBlob(legacyBlob.getData(), legacyBlob.getSize());
  CHECK(legacy.has_value());
  CHECK(legacy && legacy->strips.size() == 1);
  CHECK(legacy && legacy->strips.front().active);
}

} // namespace

int main() {
  std::cout << "===== State Restore Tests =====" << std::endl;
  testActiveStateRoundTripsThroughDeserialization();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
