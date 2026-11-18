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

  // Describe on the main thread, as Dorico and auval do. Vienna Performer,
  // created from another thread, hands its setup to the main thread and
  // waits for it forever.
  juce::MessageManager::getInstance();
  const juce::String bundle = juce::String::fromUTF8(argv[1]);
  const juce::File resultFile(juce::String::fromUTF8(argv[2]));

  bool written = false;
  {
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> results;
    format.findAllTypesForFile(results, bundle);
    written = fiddle::writePluginScanResult(resultFile, results);
  }

  std::_Exit(written ? 0 : 1);
}
