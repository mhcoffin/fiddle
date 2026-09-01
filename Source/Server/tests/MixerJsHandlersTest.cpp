#include "../MessageRouter.h"
#include "../MixerCommands.h"
#include "../MixerJsHandlers.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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

juce::var payload(std::initializer_list<juce::var> values) {
  juce::Array<juce::var> arguments;
  for (const auto &value : values)
    arguments.add(value);
  return juce::var(arguments);
}

class FakeMixerCommands final : public fiddle::MixerCommands {
public:
  bool result = true;
  std::vector<std::string> calls;
  std::vector<juce::String> stripIds;
  juce::String stripId;
  juce::String library;
  int firstInt = 0;
  int secondInt = 0;
  float number = 0.0f;
  bool flag = false;

  bool addStrip() override { return record("add"); }
  bool duplicateStrip(const juce::String &id) override {
    stripId = id;
    return record("duplicate");
  }
  bool removeStrip(const juce::String &id) override {
    stripId = id;
    return record("remove");
  }
  bool setLibrary(const juce::String &id,
                  const juce::String &newLibrary) override {
    stripId = id;
    library = newLibrary;
    return record("library");
  }
  bool setInput(const juce::String &id, int port, int channel) override {
    stripId = id;
    firstInt = port;
    secondInt = channel;
    return record("input");
  }
  bool setGain(const juce::String &id, float gainDb) override {
    stripId = id;
    number = gainDb;
    return record("gain");
  }
  bool setMute(const juce::String &id, bool muted) override {
    stripId = id;
    flag = muted;
    return record("mute");
  }
  bool setSolo(const juce::String &id, bool soloed) override {
    stripId = id;
    flag = soloed;
    return record("solo");
  }
  bool toggleLibraryActive(const juce::String &targetLibrary) override {
    library = targetLibrary;
    return record("toggle-library");
  }
  bool setProgram(const juce::String &id, int programIndex) override {
    stripId = id;
    firstInt = programIndex;
    return record("program");
  }
  bool setGroupGainDelta(const std::vector<juce::String> &ids,
                         float deltaDb) override {
    stripIds = ids;
    number = deltaDb;
    return record("group-gain-delta");
  }
  bool setGroupGainAbsolute(const std::vector<juce::String> &ids,
                            float gainDb) override {
    stripIds = ids;
    number = gainDb;
    return record("group-gain-absolute");
  }
  bool setGroupLibrary(const std::vector<juce::String> &ids,
                       const juce::String &newLibrary) override {
    stripIds = ids;
    library = newLibrary;
    return record("group-library");
  }
  bool removeGroupStrips(const std::vector<juce::String> &ids) override {
    stripIds = ids;
    return record("group-remove");
  }

private:
  bool record(std::string name) {
    calls.push_back(std::move(name));
    return result;
  }
};

void testCommandsAreRegisteredAndPayloadsAreAdapted() {
  fiddle::MessageRouter router;
  FakeMixerCommands commands;
  int changed = 0;
  std::vector<std::string> effects;
  fiddle::MixerJsHandlers handlers(
      router, commands,
      {[](fiddle::MixerJsHandlers::Task task) { task(); },
       [&] {
         ++changed;
         effects.emplace_back("changed");
       },
       [&] { effects.emplace_back("persist"); }});
  handlers.registerHandlers();

  CHECK(!router.handleMessage("notACommand", {}));
  CHECK(router.handleMessage("addMixerStrip", {}));
  CHECK(commands.calls.back() == "add");

  CHECK(router.handleMessage("duplicateStripInput", payload({"strip-a"})));
  CHECK(commands.calls.back() == "duplicate");
  CHECK(commands.stripId == "strip-a");

  CHECK(router.handleMessage("removeMixerStrip", payload({"strip-b"})));
  CHECK(commands.calls.back() == "remove");

  CHECK(router.handleMessage("setStripLibrary",
                             payload({"strip-a", "Berlin"})));
  CHECK(commands.calls.back() == "library");
  CHECK(commands.library == "Berlin");

  CHECK(router.handleMessage("setStripInput", payload({"strip-a", 2, 15})));
  CHECK(commands.calls.back() == "input");
  CHECK(commands.firstInt == 2);
  CHECK(commands.secondInt == 15);

  CHECK(router.handleMessage("setStripGain", payload({"strip-a", 18.0})));
  CHECK(commands.calls.back() == "gain");
  CHECK(std::abs(commands.number - 6.0f) < 0.001f);

  CHECK(router.handleMessage("setStripMute", payload({"strip-a", true})));
  CHECK(commands.calls.back() == "mute");
  CHECK(commands.flag);

  CHECK(router.handleMessage("setStripSolo", payload({"strip-a", false})));
  CHECK(commands.calls.back() == "solo");
  CHECK(!commands.flag);

  effects.clear();
  CHECK(router.handleMessage("toggleLibraryActive", payload({"Berlin"})));
  CHECK(commands.calls.back() == "toggle-library");
  CHECK(effects == std::vector<std::string>({"persist", "changed"}));

  CHECK(router.handleMessage("setStripProgram", payload({"strip-a", 7})));
  CHECK(commands.calls.back() == "program");
  CHECK(commands.firstInt == 7);

  const juce::String idsJson = R"(["strip-a","strip-b"])";
  CHECK(router.handleMessage("setGroupGainDelta", payload({idsJson, -3.5})));
  CHECK(commands.calls.back() == "group-gain-delta");
  CHECK(commands.stripIds.size() == 2);
  CHECK(commands.stripIds[0] == "strip-a");
  CHECK(std::abs(commands.number - -3.5f) < 0.001f);

  CHECK(router.handleMessage("setGroupGainAbsolute", payload({idsJson, -180.0})));
  CHECK(commands.calls.back() == "group-gain-absolute");
  CHECK(std::abs(commands.number - -120.0f) < 0.001f);

  CHECK(router.handleMessage("setGroupLibrary",
                             payload({idsJson, "Synchron"})));
  CHECK(commands.calls.back() == "group-library");
  CHECK(commands.library == "Synchron");

  CHECK(router.handleMessage("removeGroupStrips", payload({idsJson})));
  CHECK(commands.calls.back() == "group-remove");
  CHECK(commands.stripIds.size() == 2);

  CHECK(commands.calls.size() == 14);
  CHECK(changed == 14);
}

void testDispatchAndFailedCommandsControlSideEffects() {
  fiddle::MessageRouter router;
  FakeMixerCommands commands;
  std::vector<fiddle::MixerJsHandlers::Task> pending;
  int changed = 0;
  int persisted = 0;
  fiddle::MixerJsHandlers handlers(
      router, commands,
      {[&](fiddle::MixerJsHandlers::Task task) {
         pending.push_back(std::move(task));
       },
       [&] { ++changed; }, [&] { ++persisted; }});
  handlers.registerHandlers();

  CHECK(router.handleMessage("setStripInput", payload({"strip-a", 1, 4})));
  CHECK(commands.calls.empty());
  CHECK(pending.size() == 1);
  pending.back()();
  CHECK(commands.calls == std::vector<std::string>({"input"}));
  CHECK(changed == 1);

  commands.result = false;
  CHECK(router.handleMessage("toggleLibraryActive", payload({"Missing"})));
  CHECK(pending.size() == 2);
  pending.back()();
  CHECK(changed == 1);
  CHECK(persisted == 0);
}

void testMalformedPayloadsDoNotDispatch() {
  fiddle::MessageRouter router;
  FakeMixerCommands commands;
  int dispatched = 0;
  fiddle::MixerJsHandlers handlers(
      router, commands,
      {[&](fiddle::MixerJsHandlers::Task task) {
         ++dispatched;
         task();
       },
       {}, {}});
  handlers.registerHandlers();

  CHECK(router.handleMessage("setStripGain", payload({"strip-a"})));
  CHECK(router.handleMessage("setStripInput", "not-an-array"));
  CHECK(router.handleMessage("setGroupLibrary",
                             payload({"not-json", "Berlin"})));
  CHECK(router.handleMessage("removeGroupStrips", payload({"[]"})));
  CHECK(dispatched == 0);
  CHECK(commands.calls.empty());
}

} // namespace

int main() {
  std::cout << "===== Mixer JavaScript Handler Tests =====" << std::endl;

  testCommandsAreRegisteredAndPayloadsAreAdapted();
  testDispatchAndFailedCommandsControlSideEffects();
  testMalformedPayloadsDoNotDispatch();

  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
