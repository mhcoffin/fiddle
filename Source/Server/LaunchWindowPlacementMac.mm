#include "LaunchWindowPlacement.h"

#import <AppKit/AppKit.h>

namespace fiddle {

void settleLaunchPlacementOnFirstActivation(int delayMs) {
  auto settleSoon = [delayMs] {
    juce::Timer::callAfterDelay(
        delayMs, [] { LaunchWindowPlacement::instance().settle(); });
  };
  if ([NSApp isActive]) {
    settleSoon();
    return;
  }
  // Launched in the background: wait for the user to bring Fiddle forward
  // (dragging it into a Stage Manager group activates it).
  __block id observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:NSApplicationDidBecomeActiveNotification
                  object:NSApp
                   queue:[NSOperationQueue mainQueue]
              usingBlock:^(NSNotification *) {
                [[NSNotificationCenter defaultCenter] removeObserver:observer];
                observer = nil;
                settleSoon();
              }];
}

} // namespace fiddle
