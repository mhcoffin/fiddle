#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace fiddle {

/// Keeps reopened windows where they were saved until the user first brings
/// Fiddle forward.
///
/// Restart Fiddle relaunches in the background (open -g), so Stage Manager
/// parks Fiddle in its strip. When the user drags Fiddle into a group, Stage
/// Manager leaves the windows wherever they were dropped. Those moves are the
/// system's, not the user's: until Fiddle has been active, the saved geometry
/// stays as it was at launch, and shortly after activation every reopened
/// window goes back to it.
class LaunchWindowPlacement {
public:
  static LaunchWindowPlacement &instance();

  /// Note where a window reopened at launch belongs. Ignored once settled.
  void remember(juce::Component &window, juce::Rectangle<int> bounds);
  /// The geometry to persist for this window: its launch bounds until
  /// settled, its current bounds afterwards (or if it was never remembered).
  juce::Rectangle<int> boundsToSave(const juce::Component &window) const;
  /// Return each remembered window to its launch bounds and resume saving
  /// real positions. Later calls do nothing.
  void settle();
  bool isSettled() const noexcept { return settled_; }

private:
  struct Entry {
    juce::Component::SafePointer<juce::Component> window;
    juce::Rectangle<int> bounds;
  };
  std::vector<Entry> entries_;
  bool settled_ = false;
};

/// Settles LaunchWindowPlacement `delayMs` after the application first
/// becomes active (counting from now if it already is), leaving Stage
/// Manager's placement animation time to finish.
void settleLaunchPlacementOnFirstActivation(int delayMs);

} // namespace fiddle
