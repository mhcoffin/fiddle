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
  // Not active yet (activation follows launch, or the user launched it
  // behind another app): wait until Fiddle comes forward.
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
