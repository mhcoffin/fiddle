#include "MasterLoudnessMeter.h"
#include "StereoMeter.h"
#include <ebur128.h>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; } } while (false)
#define NEAR(x, y, tolerance) CHECK(std::abs((x) - (y)) <= (tolerance))
template<class Predicate> bool waitFor(Predicate predicate) {
  for (int i = 0; i < 1000; ++i) {
    if (predicate()) return true;
    juce::Thread::sleep(2);
  }
  return false;
}

void stereo() {
  fiddle::StereoMeter meter;
  juce::AudioBuffer<float> audio(2, 480);
  juce::FloatVectorOperations::fill(audio.getWritePointer(0), .5f, 480);
  juce::FloatVectorOperations::fill(audio.getWritePointer(1), .125f, 480);
  meter.process(&audio, 480, 48000);
  NEAR(meter.peak(0), -6.0206, .001);
  NEAR(meter.peak(1), -18.0618, .001);
  NEAR(audio.getSample(0, 20), .5, 0); // analysis is read-only
  audio.setSample(1, 17, -1.0f);
  meter.process(&audio, 480, 48000);
  CHECK(meter.clips() == 2);
  CHECK(static_cast<bool>(meter.toJson()[5]));
  meter.process(nullptr, 480, 48000);
  NEAR(meter.peak(1), -.2, .001);
  NEAR(meter.hold(1), -.03, .001);
  CHECK(meter.clips() == 2);
  meter.resetClips(); CHECK(meter.clips() == 0);
  audio.setSample(0, 0, 1.2f);
  meter.process(&audio, 480, 48000);
  CHECK(meter.clips() == 3);
  for (int i = 0; i < 5000; ++i) meter.process(nullptr, 480, 48000);
  NEAR(meter.peak(), -120, .001);
  CHECK(meter.clips() == 3); // no timeout on a latched overload
}

void loudness(unsigned long rate) {
  fiddle::MasterLoudnessMeter meter;
  meter.prepare(rate);
  CHECK(waitFor([&] { return static_cast<bool>(meter.toJson()["valid"]); }));
  CHECK(meter.toJson()["integrated"].isVoid());
  auto *reference = ebur128_init(2, rate, EBUR128_MODE_I | EBUR128_MODE_S | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_HISTOGRAM);
  CHECK(reference != nullptr);
  if (!reference) return;
  // Unequal channels, irregular callback sizes, and a final partial block.
  const int total = static_cast<int>(rate * 3 + 137);
  int offset = 0;
  while (offset < total) {
    const int count = std::min(777, total - offset);
    juce::AudioBuffer<float> audio(2, count);
    std::vector<float> interleaved(count * 2);
    for (int i = 0; i < count; ++i) {
      const float sample = .1f * std::sin(2 * juce::MathConstants<double>::pi * 1000 * (offset + i) / rate);
      audio.setSample(0, i, sample); audio.setSample(1, i, sample * .5f);
      interleaved[2 * i] = sample; interleaved[2 * i + 1] = sample * .5f;
    }
    meter.submit(audio);
    ebur128_add_frames_float(reference, interleaved.data(), count);
    offset += count;
    if (offset % 31080 < 777)
      CHECK(waitFor([&] { return static_cast<double>(meter.toJson()["seconds"]) >= double(offset) / rate; }));
  }
  CHECK(waitFor([&] { return static_cast<double>(meter.toJson()["seconds"]) >= double(total) / rate; }));
  const auto result = meter.toJson();
  CHECK(static_cast<bool>(result["valid"]));
  double expected;
  ebur128_loudness_global(reference, &expected);
  NEAR(static_cast<double>(result["integrated"]), expected, .001);
  NEAR(expected, -22.05, .15); // stereo 1 kHz, -20 dBFS left / -26.02 right
  ebur128_loudness_shortterm(reference, &expected);
  NEAR(static_cast<double>(result["shortTerm"]), expected, .001);
  ebur128_true_peak(reference, 0, &expected);
  NEAR(static_cast<double>(result["truePeakLeft"]), 20 * std::log10(expected), .001);
  ebur128_true_peak(reference, 1, &expected);
  NEAR(static_cast<double>(result["truePeakRight"]), 20 * std::log10(expected), .001);
  ebur128_destroy(&reference);

  meter.reset();
  CHECK(waitFor([&] { auto s = meter.toJson(); return static_cast<bool>(s["valid"]) && static_cast<double>(s["seconds"]) == 0; }));
  CHECK(meter.toJson()["integrated"].isVoid());
  CHECK(meter.toJson()["truePeakLeft"].isVoid());
  // A quarter-rate sine has peaks between samples: sample peaks < 0 dBFS,
  // reconstructed peak > 0 dBTP. Neither meter changes or limits the samples.
  juce::AudioBuffer<float> intersample(2, 4096);
  for (int i = 0; i < 4096; ++i) {
    const float sample = 1.2f * std::sin(juce::MathConstants<double>::pi * (.5 * i + .25));
    intersample.setSample(0, i, sample); intersample.setSample(1, i, sample);
  }
  CHECK(intersample.getMagnitude(0, 4096) < 1);
  meter.submit(intersample);
  CHECK(waitFor([&] { return static_cast<double>(meter.toJson()["seconds"]) >= 4096.0 / rate; }));
  CHECK(static_cast<double>(meter.toJson()["truePeakLeft"]) > 1.0);

  // Reset while old packets are pending: their epoch must never enter the new run.
  for (int i = 0; i < 20; ++i) meter.submit(intersample);
  meter.reset();
  juce::AudioBuffer<float> silence(2, 512); silence.clear(); meter.submit(silence);
  CHECK(waitFor([&] { auto s = meter.toJson(); return static_cast<bool>(s["valid"]) && static_cast<double>(s["seconds"]) == 512.0 / rate; }));
  CHECK(meter.toJson()["truePeakLeft"].isVoid());
  // Stop the consumer to deterministically exhaust the bounded queue.
  meter.stop();
  for (int i = 0; i < 600; ++i) meter.submit(silence);
  CHECK(!static_cast<bool>(meter.toJson()["valid"]));
  CHECK(static_cast<bool>(meter.toJson()["incomplete"]));
  CHECK(static_cast<juce::int64>(meter.toJson()["droppedFrames"]) > 0);
  CHECK(meter.toJson()["integrated"].isVoid());
  meter.prepare(rate); // device reconfiguration clears queue/history
  CHECK(waitFor([&] { return static_cast<bool>(meter.toJson()["valid"]); }));
  silence.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
  meter.submit(silence);
  CHECK(waitFor([&] { return static_cast<bool>(meter.toJson()["incomplete"]); }));
  CHECK(meter.toJson()["truePeakLeft"].isVoid());
}
}
int main() {
  stereo();
  for (const auto rate : {44100ul, 48000ul, 96000ul}) loudness(rate);
  std::cout << "Metering failures: " << failures << '\n';
  return failures ? 1 : 0;
}
