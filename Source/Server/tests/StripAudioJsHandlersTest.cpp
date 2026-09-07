#include "../MessageRouter.h"
#include "../StripAudioCommands.h"
#include "../StripAudioJsHandlers.h"

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
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__ << "]: "          \
                << #condition << std::endl;                                    \
    }                                                                          \
  } while (false)

juce::var payload(std::initializer_list<juce::var> values) {
  juce::Array<juce::var> result;
  for (const auto &value : values)
    result.add(value);
  return juce::var(result);
}

class FakeCommands final : public fiddle::StripAudioCommands {
public:
  juce::var state(const juce::String &id) const override {
    auto *object = new juce::DynamicObject();
    object->setProperty("stripId", id);
    return juce::var(object);
  }
  bool addInsert(const juce::String &strip, int uid,
                 fiddle::StripInsertPosition value) override {
    stripId = strip;
    number = uid;
    position = value;
    return record("add");
  }
  bool removeInsert(const juce::String &strip,
                    const juce::String &slot) override {
    stripId = strip;
    slotId = slot;
    return record("remove");
  }
  bool moveInsert(const juce::String &strip, const juce::String &slot,
                  fiddle::StripInsertPosition value, int index) override {
    stripId = strip;
    slotId = slot;
    position = value;
    number = index;
    return record("move");
  }
  bool setInsertBypassed(const juce::String &strip,
                         const juce::String &slot, bool value) override {
    stripId = strip;
    slotId = slot;
    bypassed = value;
    return record("bypass");
  }
  bool showInsertEditor(const juce::String &strip,
                        const juce::String &slot) override {
    stripId = strip;
    slotId = slot;
    return record("editor");
  }
  bool toggleInsertEditor(const juce::String &strip,
                          const juce::String &slot) override {
    stripId = strip;
    slotId = slot;
    return record("toggle-editor");
  }

  std::vector<std::string> calls;
  juce::String stripId;
  juce::String slotId;
  int number = 0;
  bool bypassed = false;
  fiddle::StripInsertPosition position =
      fiddle::StripInsertPosition::preFader;

private:
  bool record(std::string name) {
    calls.push_back(std::move(name));
    return true;
  }
};

void testRouting() {
  fiddle::MessageRouter router;
  FakeCommands commands;
  int published = 0;
  fiddle::StripAudioJsHandlers handlers(
      router, commands,
      {[](fiddle::StripAudioJsHandlers::Task task) { task(); },
       [&](const juce::var &state) {
         CHECK(state.getProperty("stripId", {}).toString() == "strip-a");
         ++published;
       }});
  handlers.registerHandlers();

  CHECK(router.handleMessage("requestStripAudioState", payload({"strip-a"})));
  CHECK(published == 1);
  CHECK(router.handleMessage("addStripInsert",
                             payload({"strip-a", 42, "postFader"})));
  CHECK(commands.calls.back() == "add");
  CHECK(commands.number == 42);
  CHECK(commands.position == fiddle::StripInsertPosition::postFader);
  CHECK(router.handleMessage("moveStripInsert",
                             payload({"strip-a", "slot-a", "preFader", 1})));
  CHECK(commands.calls.back() == "move");
  CHECK(commands.number == 1);
  CHECK(commands.position == fiddle::StripInsertPosition::preFader);
  CHECK(router.handleMessage("setStripInsertBypassed",
                             payload({"strip-a", "slot-a", true})));
  CHECK(commands.calls.back() == "bypass");
  CHECK(commands.bypassed);
  CHECK(router.handleMessage("showStripInsertEditor",
                             payload({"strip-a", "slot-a"})));
  CHECK(commands.calls.back() == "editor");
  CHECK(router.handleMessage("toggleStripInsertEditor",
                             payload({"strip-a", "slot-a"})));
  CHECK(commands.calls.back() == "toggle-editor");
  CHECK(router.handleMessage("removeStripInsert",
                             payload({"strip-a", "slot-a"})));
  CHECK(commands.calls.back() == "remove");
}

void testMalformedPayloads() {
  fiddle::MessageRouter router;
  FakeCommands commands;
  int dispatched = 0;
  fiddle::StripAudioJsHandlers handlers(
      router, commands,
      {[&](fiddle::StripAudioJsHandlers::Task task) {
         ++dispatched;
         task();
       },
       {}});
  handlers.registerHandlers();
  CHECK(router.handleMessage("requestStripAudioState", {}));
  CHECK(router.handleMessage("addStripInsert", payload({"strip-a", 2})));
  CHECK(router.handleMessage("moveStripInsert", payload({"strip-a"})));
  CHECK(router.handleMessage("setStripInsertBypassed", "bad"));
  CHECK(router.handleMessage("addStripInsert",
                             payload({"strip-a", 2, "sideways"})));
  CHECK(dispatched == 0);
}

} // namespace

int main() {
  std::cout << "===== Strip Audio JavaScript Handler Tests =====\n";
  testRouting();
  testMalformedPayloads();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
