#include "../MasterAudioJsHandlers.h"
#include "../MasterAudioCommands.h"
#include "../MessageRouter.h"

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
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                     \
                << "]: " << #condition << std::endl;                           \
    }                                                                          \
  } while (false)

juce::var payload(std::initializer_list<juce::var> values) {
  juce::Array<juce::var> result;
  for (const auto &value : values)
    result.add(value);
  return juce::var(result);
}

class FakeMasterAudioCommands final : public fiddle::MasterAudioCommands {
public:
  juce::var state() const override {
    auto *object = new juce::DynamicObject();
    object->setProperty("id", "master");
    return juce::var(object);
  }
  bool addInsert(int uid) override {
    number = static_cast<float>(uid);
    return record("add");
  }
  bool removeInsert(const juce::String &id) override {
    slotId = id;
    return record("remove");
  }
  bool moveInsert(const juce::String &id, int index) override {
    slotId = id;
    number = static_cast<float>(index);
    return record("move");
  }
  bool setInsertBypassed(const juce::String &id, bool value) override {
    slotId = id;
    bypassed = value;
    return record("bypass");
  }
  bool showInsertEditor(const juce::String &id) override {
    slotId = id;
    return record("editor");
  }
  bool setGainDb(float value) override {
    number = value;
    return record("gain");
  }

  std::vector<std::string> calls;
  juce::String slotId;
  float number = 0.0f;
  bool bypassed = false;

private:
  bool record(std::string name) {
    calls.push_back(std::move(name));
    return true;
  }
};

void testMasterAudioCommandsAreRouted() {
  fiddle::MessageRouter router;
  FakeMasterAudioCommands commands;
  int published = 0;
  fiddle::MasterAudioJsHandlers handlers(
      router, commands,
      {[](fiddle::MasterAudioJsHandlers::Task task) { task(); },
       [&](const juce::var &state) {
         CHECK(state.getProperty("id", {}).toString() == "master");
         ++published;
       }});
  handlers.registerHandlers();

  CHECK(router.handleMessage("requestMasterAudioState", {}));
  CHECK(published == 1);
  CHECK(router.handleMessage("addMasterInsert", payload({42})));
  CHECK(commands.calls.back() == "add");
  CHECK(commands.number == 42.0f);
  CHECK(router.handleMessage("removeMasterInsert", payload({"slot-a"})));
  CHECK(commands.slotId == "slot-a");
  CHECK(router.handleMessage("moveMasterInsert", payload({"slot-a", 2})));
  CHECK(commands.calls.back() == "move");
  CHECK(commands.number == 2.0f);
  CHECK(router.handleMessage("setMasterInsertBypassed",
                             payload({"slot-a", true})));
  CHECK(commands.calls.back() == "bypass");
  CHECK(commands.bypassed);
  CHECK(router.handleMessage("showMasterInsertEditor", payload({"slot-a"})));
  CHECK(commands.calls.back() == "editor");
  CHECK(router.handleMessage("setMasterGain", payload({-4.25})));
  CHECK(commands.calls.back() == "gain");
  CHECK(std::abs(commands.number - -4.25f) < 0.001f);
}

void testMalformedPayloadsDoNotDispatch() {
  fiddle::MessageRouter router;
  FakeMasterAudioCommands commands;
  int dispatched = 0;
  fiddle::MasterAudioJsHandlers handlers(
      router, commands,
      {[&](fiddle::MasterAudioJsHandlers::Task task) {
         ++dispatched;
         task();
       },
       {}});
  handlers.registerHandlers();

  CHECK(router.handleMessage("addMasterInsert", {}));
  CHECK(router.handleMessage("moveMasterInsert", payload({"slot-a"})));
  CHECK(router.handleMessage("setMasterInsertBypassed", "bad-payload"));
  CHECK(router.handleMessage("setMasterGain", payload({})));
  CHECK(dispatched == 0);
  CHECK(commands.calls.empty());
}

} // namespace

int main() {
  std::cout << "===== Master Audio JavaScript Handler Tests =====\n";
  testMasterAudioCommandsAreRouted();
  testMalformedPayloadsDoNotDispatch();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
