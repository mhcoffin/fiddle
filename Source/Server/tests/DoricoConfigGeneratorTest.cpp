#include "../DoricoConfigGenerator.h"

#include <iostream>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                    \
                << "]: " << #condition << std::endl;                          \
    }                                                                          \
  } while (false)

void testSparseStableDestinationsAreUsedAsEndpointNumbers() {
  std::vector<fiddle::InstrumentAssignment> assignments;

  fiddle::InstrumentAssignment violin;
  violin.entityID = "instrument.strings.violin";
  violin.name = "Violin 1";
  violin.isSolo = false;
  violin.flatIndex = 1; // Port 1, channel 2; channel 1 is the metronome.
  assignments.push_back(violin);

  fiddle::InstrumentAssignment flute;
  flute.entityID = "instrument.wind.flute";
  flute.name = "Flute 1";
  flute.isSolo = true;
  flute.flatIndex = 18; // Port 2, channel 3, with a deliberate gap.
  assignments.push_back(flute);

  auto xml = fiddle::DoricoConfigGenerator::buildEndpointConfigXml(
      assignments, {});
  CHECK(xml != nullptr);

  auto *slots = xml ? xml->getChildByName("slots") : nullptr;
  auto *slot = slots ? slots->getChildByName("slotData") : nullptr;
  auto *instance = slot ? slot->getChildByName("instanceData") : nullptr;
  auto *contents =
      instance ? instance->getChildByName("programContents") : nullptr;
  auto *entries = contents ? contents->getChildByName("entries") : nullptr;
  auto *firstEntry = entries ? entries->getFirstChildElement() : nullptr;
  auto *secondEntry = firstEntry ? firstEntry->getNextElement() : nullptr;
  CHECK(firstEntry != nullptr);
  CHECK(firstEntry &&
        firstEntry->getChildElementAllSubText("portIndex", "") == "0");
  CHECK(firstEntry && firstEntry->getChildElementAllSubText(
                            "channelNumberRel0", "") == "1");
  CHECK(secondEntry &&
        secondEntry->getChildElementAllSubText("portIndex", "") == "1");
  CHECK(secondEntry && secondEntry->getChildElementAllSubText(
                             "channelNumberRel0", "") == "2");

  auto *instruments = xml ? xml->getChildByName("instruments") : nullptr;
  auto *firstInstrument =
      instruments ? instruments->getFirstChildElement() : nullptr;
  auto *secondInstrument =
      firstInstrument ? firstInstrument->getNextElement() : nullptr;
  CHECK(firstInstrument != nullptr);
  CHECK(firstInstrument && firstInstrument->getChildElementAllSubText(
                               "endpoints", "") == "1");
  CHECK(secondInstrument && secondInstrument->getChildElementAllSubText(
                                "endpoints", "") == "18");
}

} // namespace

int main() {
  std::cout << "===== Dorico Config Generator Tests =====" << std::endl;
  testSparseStableDestinationsAreUsedAsEndpointNumbers();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
