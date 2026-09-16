#include "../RealtimeMixPrinter.h"

#include <cmath>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      ++failures;                                                              \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__ << "]: "          \
                << #condition << '\n';                                        \
    }                                                                          \
  } while (false)
} // namespace

int main() {
  const auto directory =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("fiddle-mix-print-test-" + juce::Uuid().toString());
  CHECK(directory.createDirectory().wasOk());
  const auto file = directory.getChildFile("master.wav");

  {
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.start(file, 8000.0, 80).isEmpty());
    CHECK(static_cast<bool>(printer.state()["armed"]));
    CHECK(!static_cast<bool>(printer.state()["recording"]));

    juce::AudioBuffer<float> block(2, 80);
    for (int channel = 0; channel < block.getNumChannels(); ++channel)
      juce::FloatVectorOperations::fill(
          block.getWritePointer(channel), channel == 0 ? 0.25f : -0.5f,
          block.getNumSamples());

    // Armed audio before transport start is excluded. The start and stop
    // timestamps then trim sample-accurately inside their boundary blocks.
    CHECK(printer.push(block, 900.0));
    CHECK(printer.beginAt(1001.0));
    CHECK(printer.endAt(1027.0));
    CHECK(!printer.needsFinalizing(1027.0));
    CHECK(printer.push(block, 1000.0));
    CHECK(printer.push(block, 1010.0));
    CHECK(printer.push(block, 1020.0));
    CHECK(printer.needsFinalizing(1027.0));

    printer.stop();
    const auto state = printer.state();
    CHECK(!static_cast<bool>(state["armed"]));
    CHECK(!static_cast<bool>(state["recording"]));
    CHECK(!static_cast<bool>(state["finalizing"]));
    CHECK(static_cast<juce::int64>(state["samples"]) == 208);
    CHECK(static_cast<juce::int64>(state["droppedBlocks"]) == 0);
  }

  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
  CHECK(reader != nullptr);
  if (reader) {
    CHECK(reader->sampleRate == 8000.0);
    CHECK(reader->numChannels == 2);
    CHECK(reader->bitsPerSample == 32);
    CHECK(reader->lengthInSamples == 208);
    juce::AudioBuffer<float> audio(2, static_cast<int>(reader->lengthInSamples));
    CHECK(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true));
    CHECK(std::abs(audio.getSample(0, 200) - 0.25f) < 1.0e-6f);
    CHECK(std::abs(audio.getSample(1, 200) + 0.5f) < 1.0e-6f);
  }

  CHECK(directory.deleteRecursively());
  if (failures == 0)
    std::cout << "Real-time Master printing and WAV finalization passed\n";
  return failures == 0 ? 0 : 1;
}
