#include "../ApplicationRestart.h"

#include <iostream>

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

void testAppBundleResolution() {
  const juce::File executable(
      "/tmp/My Build/FiddleServer.app/Contents/MacOS/FiddleServer");
  const auto bundle =
      fiddle::application_restart::findContainingAppBundle(executable);
  CHECK(bundle.getFullPathName() == "/tmp/My Build/FiddleServer.app");

  const juce::File standalone("/tmp/FiddleServer");
  CHECK(fiddle::application_restart::findContainingAppBundle(standalone) ==
        juce::File{});
}

void testHelperArgumentsPreservePathWithSpaces() {
  const juce::File bundle("/tmp/My Build/FiddleServer.app");
  const auto arguments =
      fiddle::application_restart::makeMacHelperArguments(12345, bundle);

  CHECK(arguments.size() == 6);
  CHECK(arguments[0] == "/bin/sh");
  CHECK(arguments[1] == "-c");
  CHECK(arguments[2].contains("kill -0 \"$1\""));
  CHECK(arguments[2].contains("open -n \"$2\""));
  CHECK(arguments[3] == "fiddle-restart");
  CHECK(arguments[4] == "12345");
  CHECK(arguments[5] == "/tmp/My Build/FiddleServer.app");
}

} // namespace

int main() {
  std::cout << "===== Application Restart Tests =====" << std::endl;

  testAppBundleResolution();
  testHelperArgumentsPreservePathWithSpaces();

  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
