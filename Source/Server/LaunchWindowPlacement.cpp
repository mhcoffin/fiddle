#include "LaunchWindowPlacement.h"

#include <iostream>

namespace fiddle {

LaunchWindowPlacement &LaunchWindowPlacement::instance() {
  static LaunchWindowPlacement placement;
  return placement;
}

void LaunchWindowPlacement::remember(juce::Component &window,
                                     juce::Rectangle<int> bounds) {
  if (settled_)
    return;
  for (auto &entry : entries_)
    if (entry.window.getComponent() == &window) {
      entry.bounds = bounds;
      return;
    }
  entries_.push_back({juce::Component::SafePointer<juce::Component>(&window),
                      bounds});
}

juce::Rectangle<int>
LaunchWindowPlacement::boundsToSave(const juce::Component &window) const {
  if (!settled_)
    for (const auto &entry : entries_)
      if (entry.window.getComponent() == &window)
        return entry.bounds;
  return window.getBounds();
}

void LaunchWindowPlacement::settle() {
  if (settled_)
    return;
  // Settle first: the moves below save through boundsToSave().
  settled_ = true;
  auto entries = std::move(entries_);
  entries_.clear();
  for (auto &entry : entries) {
    auto *window = entry.window.getComponent();
    if (window == nullptr || window->getBounds() == entry.bounds)
      continue;
    std::cerr << "[Windows] Returning '" << window->getName() << "' from "
              << window->getBounds().toString() << " to "
              << entry.bounds.toString() << std::endl;
    window->setBounds(entry.bounds);
  }
}

#if !JUCE_MAC
void settleLaunchPlacementOnFirstActivation(int) {
  LaunchWindowPlacement::instance().settle();
}
#endif

} // namespace fiddle
