#include "../GroupBusCommands.h"
#include "../GroupBusJsHandlers.h"
#include "../MessageRouter.h"

#include <cmath>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; } } while (false)

juce::var payload(std::initializer_list<juce::var> values) {
  juce::Array<juce::var> result;
  for (const auto &value : values) result.add(value);
  return juce::var(result);
}

class FakeCommands final : public fiddle::GroupBusCommands {
public:
  juce::String call, id, value;
  std::vector<juce::String> ids;
  int index = 0;
  float gain = 0.0f;
  bool flag = false;
  bool result = true;
  bool addGroupBus(const juce::String &name, const std::vector<juce::String> &stripIds) override { call="add"; value=name; ids=stripIds; return result; }
  bool removeGroupBus(const juce::String &busId) override { call="remove"; id=busId; return result; }
  bool renameGroupBus(const juce::String &busId, const juce::String &name) override { call="rename"; id=busId; value=name; return result; }
  bool moveGroupBus(const juce::String &busId, int newIndex) override { call="move"; id=busId; index=newIndex; return result; }
  bool setGroupBusGain(const juce::String &busId, float gainDb) override { call="gain"; id=busId; gain=gainDb; return result; }
  bool setGroupBusMute(const juce::String &busId, bool muted) override { call="mute"; id=busId; flag=muted; return result; }
  bool setGroupBusSolo(const juce::String &busId, bool soloed) override { call="solo"; id=busId; flag=soloed; return result; }
  bool addGroupBusInsert(const juce::String &busId, int pluginUid, fiddle::StripInsertPosition position) override { call="addInsert"; id=busId; index=pluginUid; insertPosition=position; return result; }
  bool removeGroupBusInsert(const juce::String &busId, const juce::String &slotId) override { call="removeInsert"; id=busId; value=slotId; return result; }
  bool moveGroupBusInsert(const juce::String &busId, const juce::String &slotId, fiddle::StripInsertPosition position, int newIndex) override { call="moveInsert"; id=busId; value=slotId; insertPosition=position; index=newIndex; return result; }
  bool setGroupBusInsertBypassed(const juce::String &busId, const juce::String &slotId, bool bypassed) override { call="bypassInsert"; id=busId; value=slotId; flag=bypassed; return result; }
  bool toggleGroupBusInsertEditor(const juce::String &busId, const juce::String &slotId) override { call="toggleEditor"; id=busId; value=slotId; return result; }
  bool setStripDirectOutput(const juce::String &stripId, const juce::String &busId) override { call="output"; id=stripId; value=busId; return result; }
  fiddle::StripInsertPosition insertPosition = fiddle::StripInsertPosition::preFader;
};

void run() {
  fiddle::MessageRouter router;
  FakeCommands commands;
  int changed = 0, requested = 0;
  fiddle::GroupBusJsHandlers handlers(router, commands,
      {[](auto task) { task(); }, [&] { ++changed; }, [&] { ++requested; }});
  handlers.registerHandlers();
  CHECK(router.handleMessage("requestGroupBusState", {}));
  CHECK(requested == 1);
  CHECK(router.handleMessage("addGroupBus", payload({"Strings", R"(["a","b"])"})));
  CHECK(commands.call == "add" && commands.value == "Strings" && commands.ids.size() == 2);
  CHECK(router.handleMessage("renameGroupBus", payload({"bus", "Winds"})));
  CHECK(commands.call == "rename" && commands.id == "bus" && commands.value == "Winds");
  CHECK(router.handleMessage("moveGroupBus", payload({"bus", 3})));
  CHECK(commands.call == "move" && commands.index == 3);
  CHECK(router.handleMessage("setGroupBusGain", payload({"bus", -4.5})));
  CHECK(commands.call == "gain" && std::abs(commands.gain + 4.5f) < 0.001f);
  CHECK(router.handleMessage("setGroupBusMute", payload({"bus", true})));
  CHECK(commands.call == "mute" && commands.flag);
  CHECK(router.handleMessage("setGroupBusSolo", payload({"bus", true})));
  CHECK(commands.call == "solo" && commands.flag);
  CHECK(router.handleMessage("addGroupBusInsert", payload({"bus", 42, "postFader"})));
  CHECK(commands.call == "addInsert" && commands.id == "bus" && commands.index == 42 && commands.insertPosition == fiddle::StripInsertPosition::postFader);
  CHECK(router.handleMessage("moveGroupBusInsert", payload({"bus", "slot", "preFader", 2})));
  CHECK(commands.call == "moveInsert" && commands.value == "slot" && commands.index == 2 && commands.insertPosition == fiddle::StripInsertPosition::preFader);
  CHECK(router.handleMessage("setGroupBusInsertBypassed", payload({"bus", "slot", true})));
  CHECK(commands.call == "bypassInsert" && commands.flag);
  CHECK(router.handleMessage("removeGroupBusInsert", payload({"bus", "slot"})));
  CHECK(commands.call == "removeInsert");
  CHECK(router.handleMessage("toggleGroupBusInsertEditor", payload({"bus", "slot"})));
  CHECK(commands.call == "toggleEditor" && requested == 2);
  CHECK(router.handleMessage("setStripDirectOutput", payload({"strip", "bus"})));
  CHECK(commands.call == "output" && commands.id == "strip" && commands.value == "bus");
  CHECK(router.handleMessage("removeGroupBus", payload({"bus"})));
  CHECK(commands.call == "remove");
  CHECK(changed == 12);
  commands.result = false;
  CHECK(router.handleMessage("removeGroupBus", payload({"missing"})));
  CHECK(changed == 12);
}
} // namespace

int main() {
  run();
  if (!failures) std::cout << "GroupBusJsHandlersTest passed\n";
  return failures == 0 ? 0 : 1;
}
