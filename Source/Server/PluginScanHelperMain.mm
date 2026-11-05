// FiddlePluginScan: describes one VST3 bundle on behalf of FiddleServer.
//
//   FiddlePluginScan <bundle.vst3> <result.xml>
//
// Describing a VST3 without moduleinfo.json instantiates it. Doing that in its
// own process keeps a plug-in that crashes, hangs, or clashes with an instance
// already hosted by FiddleServer from taking the server down. After writing the
// result file (writePluginScanResult) the process exits without running static
// destructors, which in a plug-in's module are a common place to crash.

#include "PluginCatalogScan.h"

#import <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <thread>
#include <unistd.h>

int main(int argc, char *argv[]) {
  // FiddleServer's descriptors (listening sockets, the database) are inherited
  // across fork/exec. Close them so a stuck helper cannot hold them.
  const int maxDescriptor = std::min(getdtablesize(), 65536);
  for (int fd = 3; fd < maxDescriptor; ++fd)
    close(fd);

  if (argc != 3)
    return 64;

  @autoreleasepool {
    // A plug-in that touches NSApplication must not put a Dock icon up.
    [[NSApplication sharedApplication]
        setActivationPolicy:NSApplicationActivationPolicyProhibited];
  }

  // The main thread is the JUCE message thread and keeps its run loop turning
  // while a worker thread describes the plug-in, as it was inside FiddleServer.
  juce::MessageManager::getInstance();
  const juce::String bundle = juce::String::fromUTF8(argv[1]);
  const juce::File resultFile(juce::String::fromUTF8(argv[2]));

  std::atomic<bool> done{false};
  std::atomic<bool> written{false};
  CFRunLoopRef mainLoop = CFRunLoopGetMain();
  std::thread worker([&] {
    {
      juce::VST3PluginFormatHeadless format;
      juce::OwnedArray<juce::PluginDescription> results;
      format.findAllTypesForFile(results, bundle);
      written = fiddle::writePluginScanResult(resultFile, results);
    }
    done = true;
    CFRunLoopWakeUp(mainLoop);
  });

  while (!done)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
  worker.join();

  std::_Exit(written ? 0 : 1);
}
