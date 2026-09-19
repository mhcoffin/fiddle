#include "DebugUiFeed.h"
#include <iostream>
#include <thread>

int main() {
  int failures = 0;
  const auto check = [&](bool value) { if (!value) { ++failures; std::cerr << "Debug feed check failed\n"; } };
  fiddle::DebugUiFeed feed;
  for (int i = 0; i < 10000; ++i) feed.push("pushMidiEvent", i);
  check(feed.takeBatch().isVoid()); // hidden windows neither collect nor replay
  feed.push("setTempo", 120);
  feed.setActive(true);
  auto batch = feed.takeBatch();
  check(static_cast<bool>(batch["reset"]));
  check(batch["messages"].size() == 1);
  check(batch["messages"][0]["type"].toString() == "setTempo");
  for (int i = 0; i < 10000; ++i) feed.push("pushMidiEvent", i);
  check(feed.takeBatch().isVoid()); // still awaiting browser completion
  feed.complete();
  batch = feed.takeBatch();
  check(batch["messages"].size() == 128);
  check(static_cast<int>(batch["dropped"]) == 9872);
  check(static_cast<int>(batch["messages"][0]["data"]) == 9872);
  check(static_cast<int>(batch["messages"][127]["data"]) == 9999);
  feed.setActive(false);
  feed.setActive(true);
  check(feed.takeBatch().isVoid()); // hiding does not cancel an in-flight call
  feed.complete();
  batch = feed.takeBatch();
  check(static_cast<bool>(batch["reset"]));
  check(static_cast<int>(batch["dropped"]) == 0);
  feed.complete(false);
  check(static_cast<bool>(feed.takeBatch()["reset"]));
  feed.complete();
  feed.push("setMasterGain", 6); // only diagnostic types are accepted
  check(feed.takeBatch().isVoid());

  std::thread midi([&] { for (int i = 0; i < 20000; ++i) feed.push("updateNoteState", i); });
  std::thread logs([&] { for (int i = 0; i < 20000; ++i) feed.push("addLogMessage", i); });
  for (int i = 0; i < 500; ++i) {
    batch = feed.takeBatch();
    if (!batch.isVoid()) {
      check(batch["messages"].size() <= 128);
      feed.complete();
    }
  }
  midi.join(); logs.join();
  feed.setActive(false);
  check(feed.takeBatch().isVoid());
  std::cout << "Debug feed failures: " << failures << '\n';
  return failures ? 1 : 0;
}
