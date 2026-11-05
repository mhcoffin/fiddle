#include "../LaunchWindowPlacement.h"

#include <iostream>
#include <memory>

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

using Bounds = juce::Rectangle<int>;

// Stage Manager moves the windows of a background launch; until the user
// brings Fiddle forward, those moves must not replace the saved geometry.
void testSystemMovesBeforeActivationAreNotSaved() {
  fiddle::LaunchWindowPlacement placement;
  juce::Component main, history;
  const Bounds mainSaved(40, 60, 1200, 800), historySaved(1300, 60, 500, 700);
  main.setBounds(mainSaved);
  history.setBounds(historySaved);
  placement.remember(main, mainSaved);
  placement.remember(history, historySaved);

  // Dropped into the centre of the screen.
  main.setBounds(400, 300, 1200, 800);
  history.setBounds(500, 350, 500, 700);
  CHECK(!placement.isSettled());
  CHECK(placement.boundsToSave(main) == mainSaved);
  CHECK(placement.boundsToSave(history) == historySaved);

  placement.settle();
  CHECK(placement.isSettled());
  CHECK(main.getBounds() == mainSaved);
  CHECK(history.getBounds() == historySaved);

  // From now on the user's moves are real and are saved as they are.
  main.setBounds(10, 20, 1000, 700);
  CHECK(placement.boundsToSave(main) == Bounds(10, 20, 1000, 700));
}

void testOnlyLaunchWindowsAreHeldAndOnlyOnce() {
  fiddle::LaunchWindowPlacement placement;
  juce::Component remembered, other;
  placement.remember(remembered, {0, 0, 300, 200});
  other.setBounds(5, 5, 300, 200);
  CHECK(placement.boundsToSave(other) == Bounds(5, 5, 300, 200));

  placement.settle();
  remembered.setBounds(70, 80, 300, 200);
  placement.settle(); // a second activation must not move it back
  CHECK(remembered.getBounds() == Bounds(70, 80, 300, 200));

  placement.remember(other, {0, 0, 10, 10}); // ignored once settled
  CHECK(placement.boundsToSave(other) == Bounds(5, 5, 300, 200));
}

void testClosedWindowsAreSkipped() {
  fiddle::LaunchWindowPlacement placement;
  auto window = std::make_unique<juce::Component>();
  placement.remember(*window, {0, 0, 300, 200});
  window.reset();
  placement.settle(); // must not touch the deleted window
  CHECK(placement.isSettled());
}

} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  std::cout << "===== Launch Window Placement Tests =====" << std::endl;

  testSystemMovesBeforeActivationAreNotSaved();
  testOnlyLaunchWindowsAreHeldAndOnlyOnce();
  testClosedWindowsAreSkipped();

  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
