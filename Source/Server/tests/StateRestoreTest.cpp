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
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #condition \
                << std::endl;                                                  \
    }                                                                          \
  } while (false)

template <typename T> void append(juce::MemoryBlock &blob, T value) {
  blob.append(&value, sizeof(value));
}

void appendString(juce::MemoryBlock &blob, const std::string &value) {
  append<uint32_t>(blob, static_cast<uint32_t>(value.size()));
  blob.append(value.data(), value.size());
}

juce::MemoryBlock
makeBlobWithActive(bool active, bool includeActive = true,
                   uint32_t version = fiddle::kStateBlobVersion,
                   bool includeMasterInsert = false,
                   bool muted = false, bool soloed = false,
                   bool includeMuteSolo = false) {
  juce::MemoryBlock blob;
  append<uint32_t>(blob, fiddle::kStateBlobMagic);
  append<uint32_t>(blob, version);
  append<uint32_t>(blob, 0);
  appendString(blob, "Regression Test");
  appendString(blob, "1");
  append<uint8_t>(blob, 0);

  if (version >= 4) {
    append<uint32_t>(blob, 1);
    append<float>(blob, -3.5f);
    append<uint32_t>(blob, includeMasterInsert ? 1 : 0);
    if (includeMasterInsert) {
      appendString(
          blob,
          R"({"slotId":"master-fx-1","formatName":"VST3","pluginUid":42,"fileOrIdentifier":"/test/effect.vst3","manufacturer":"Test Maker","name":"Test Limiter","category":"Dynamics","pluginVersion":"1.2","numInputChannels":2,"numOutputChannels":2,"bypassed":true})");
      append<uint32_t>(blob, 3);
      const uint8_t pluginState[] = {4, 5, 6};
      blob.append(pluginState, sizeof(pluginState));
    }
  }

  appendString(blob, "state-hash");
  append<uint32_t>(blob, 0);
  append<uint32_t>(blob, 1);

  const std::string activeProperty =
      includeActive ? std::string{"\"active\":"} + (active ? "true," : "false,")
                    : "";
  const std::string muteSoloProperties =
      includeMuteSolo
          ? std::string{"\"muted\":"} + (muted ? "true," : "false,") +
                "\"soloed\":" + (soloed ? "true," : "false,")
          : "";
  const std::string json =
      std::string{"{\"id\":\"strip-1\",\"library\":\"Test\",\"family\":"
                  "\"Strings\",\"isSolo\":true,"} +
      activeProperty + muteSoloProperties +
      "\"inputPort\":0,\"inputChannel\":1,\"pluginUid\":0,"
      "\"gainDb\":0.0,\"expressionMap\":\"\",\"luaPlugins\":[]}";
  appendString(blob, json);
  append<uint32_t>(blob, 0);

  auto totalSize = static_cast<uint32_t>(blob.getSize() - 12);
  std::memcpy(static_cast<uint8_t *>(blob.getData()) + 8, &totalSize,
              sizeof(totalSize));
  return blob;
}

void testMuteAndSoloRoundTripWithLegacyDefaults() {
  auto persistedBlob = makeBlobWithActive(true, true,
                                          fiddle::kStateBlobVersion, false,
                                          true, true, true);
  auto persisted = fiddle::deserializeStateBlob(persistedBlob.getData(),
                                                persistedBlob.getSize());
  CHECK(persisted.has_value());
  CHECK(persisted && persisted->strips.front().muted);
  CHECK(persisted && persisted->strips.front().soloed);

  auto legacyBlob = makeBlobWithActive(true);
  auto legacy =
      fiddle::deserializeStateBlob(legacyBlob.getData(), legacyBlob.getSize());
  CHECK(legacy.has_value());
  CHECK(legacy && !legacy->strips.front().muted);
  CHECK(legacy && !legacy->strips.front().soloed);
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

void testMasterAudioRoundTripsAndV3DefaultsRemainCompatible() {
  auto masterBlob = makeBlobWithActive(true, true, 4, true);
  auto master =
      fiddle::deserializeStateBlob(masterBlob.getData(), masterBlob.getSize());
  CHECK(master.has_value());
  CHECK(master && master->audioSchemaVersion == 1);
  CHECK(master && master->masterGainDb == -3.5f);
  CHECK(master && master->masterInserts.size() == 1);
  CHECK(master && master->masterInserts.front().slotId == "master-fx-1");
  CHECK(master && master->masterInserts.front().pluginUid == 42);
  CHECK(master && master->masterInserts.front().bypassed);
  CHECK(master && master->masterInserts.front().pluginState.getSize() == 3);

  auto v3Blob = makeBlobWithActive(true, true, 3);
  auto v3 = fiddle::deserializeStateBlob(v3Blob.getData(), v3Blob.getSize());
  CHECK(v3.has_value());
  CHECK(v3 && v3->masterGainDb == 0.0f);
  CHECK(v3 && v3->masterInserts.empty());
  CHECK(v3 && v3->strips.size() == 1);
}

} // namespace

int main() {
  std::cout << "===== State Restore Tests =====" << std::endl;
  testActiveStateRoundTripsThroughDeserialization();
  testMuteAndSoloRoundTripWithLegacyDefaults();
  testMasterAudioRoundTripsAndV3DefaultsRemainCompatible();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
