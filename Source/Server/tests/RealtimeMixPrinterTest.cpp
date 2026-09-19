#include "../RealtimeMixPrinter.h"
#include "AudioStreamRing.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

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
    CHECK(printer.push(block, 900.0, 0));
    CHECK(printer.beginAt(1001.0));
    CHECK(printer.endAt(1027.0));
    CHECK(!printer.needsFinalizing(1027.0));
    CHECK(printer.push(block, 1000.0, 800));
    CHECK(printer.push(block, 1010.0, 880));
    CHECK(printer.push(block, 1020.0, 960));
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

  reader.reset();
  // Host callback time can jitter while the actual sample stream is perfectly
  // contiguous. Exercise the production planning clock with two selected buses.
  {
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.startStems(directory, 8000, 80,
        {{"a", "Strings"}, {"b", "Brass"}}, false, 0).isEmpty());
    CHECK(printer.state()["format"].toString() ==
          juce::String::fromUTF8("Stereo WAV · 32-bit float"));
    CHECK(printer.beginAt(1001));
    CHECK(printer.endAt(1027));
    auto ring = std::make_unique<fiddle::AudioStreamRing>();
    ring->sampleRate = 8000;
    ring->blockSize = ring->targetFrames = 80;
    juce::AudioBuffer<float> master(2, 80);
    for (int step = 0; step < 3; ++step) {
      ring->readFrame.store(step * 80);
      ring->publishClock(step * 80, 1000 + step * 10 + (step == 1 ? 0.2 : 0.0));
      const auto plan = fiddle::RenderAheadPlan::next(*ring);
      CHECK(plan.render && plan.skippedFrames == 0);
      printer.renderAndPush(master, plan.presentationTimeMs, plan.startFrame, [&](fiddle::BusPrintCapture *capture) {
        master.clear();
        if (!capture) { CHECK(capture != nullptr); return; }
        for (const auto &id : {"a", "b"}) {
          auto *bus = capture->targetFor(id);
          for (int ch = 0; ch < 2; ++ch)
            for (int frame = 0; frame < 80; ++frame)
              bus->setSample(ch, frame, (step * 80 + frame) / 1000.0f);
        }
      });
      CHECK(ring->push(plan.startFrame, master.getReadPointer(0), master.getReadPointer(1), 80));
    }
    printer.stop();
    CHECK(!static_cast<bool>(printer.state()["incomplete"]));
    CHECK(static_cast<juce::int64>(printer.state()["samples"]) == 208);
    const auto paths = printer.state()["files"];
    for (const auto &path : *paths.getArray()) {
      std::unique_ptr<juce::AudioFormatReader> stem(formats.createReaderFor(juce::File(path.toString())));
      CHECK(stem && stem->lengthInSamples == 208);
      if (!stem || stem->lengthInSamples != 208) continue;
      juce::AudioBuffer<float> audio(2, 208);
      CHECK(stem->read(&audio, 0, 208, 0, true, true));
      for (int ch = 0; ch < 2; ++ch)
        for (int frame = 0; frame < 208; ++frame)
          CHECK(std::abs(audio.getSample(ch, frame) - (frame + 8) / 1000.0f) < 1.e-6f);
    }
  }
  // One shared gate trims every stem identically; bus audio is aligned with
  // Master latency without being processed by Master effects or gain.
  {
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.startStems(directory, 8000, 80,
        {{"a", "Strings/Violins"}, {"b", "Strings/Violins"}}, true, 5).isEmpty());
    const auto state = printer.state();
    const auto take = juce::File(state["filePath"].toString());
    CHECK(take.getParentDirectory() == directory);
    CHECK(state["files"].size() == 3);
    CHECK(state["files"][1].toString() != state["files"][2].toString());
    for (const auto &path : *state["files"].getArray())
      CHECK(juce::File(path.toString()).getParentDirectory() == take);
    CHECK(printer.beginAt(1001));
    CHECK(!printer.beginAt(1010)); // A second start must not reset a take.
    CHECK(printer.endAt(1027));
    for (int step = 0; step < 3; ++step) {
      juce::AudioBuffer<float> master(2, 80);
      printer.renderAndPush(master, 1000.0 + step * 10, uint64_t(step * 80), [&](fiddle::BusPrintCapture *capture) {
        CHECK(capture != nullptr);
        auto *a = capture->targetFor("a");
        auto *b = capture->targetFor("b");
        for (int ch = 0; ch < 2; ++ch) {
          juce::FloatVectorOperations::fill(master.getWritePointer(ch), 0.25f, 80);
          for (int frame = 0; frame < 80; ++frame) {
            a->setSample(ch, frame, (step * 80 + frame + ch) / 1000.0f);
            b->setSample(ch, frame, -0.5f);
          }
        }
      });
    }
    printer.stop();
    CHECK(!static_cast<bool>(printer.state()["incomplete"]));
    for (int i = 0; i < 3; ++i) {
      std::unique_ptr<juce::AudioFormatReader> stem(formats.createReaderFor(juce::File(state["files"][i].toString())));
      CHECK(stem != nullptr);
      if (!stem) continue;
      CHECK(stem->lengthInSamples == 208);
      CHECK(stem->numChannels == 2 && stem->bitsPerSample == 32);
      CHECK(stem->usesFloatingPointData);
      juce::AudioBuffer<float> audio(2, 208);
      CHECK(stem->read(&audio, 0, 208, 0, true, true));
      for (int ch = 0; ch < 2; ++ch)
        for (int frame = 0; frame < 208; ++frame) {
          const auto expected = i == 0 ? 0.25f : i == 1 ? (frame + 8 - 5 + ch) / 1000.0f : -0.5f;
          CHECK(std::abs(audio.getSample(ch, frame) - expected) < 1.e-6f);
        }
    }
    CHECK(printer.startStems(directory, 8000, 80, {{"a", "Strings/Violins"}}, false, 0).isEmpty());
    CHECK(printer.state()["filePath"].toString() != take.getFullPathName());
    CHECK(printer.state()["files"].size() == 1);
    // Cancel before playback yields a valid empty file, not an audition note.
    const auto empty = juce::File(printer.state()["files"][0].toString());
    juce::AudioBuffer<float> master(2, 80);
    printer.renderAndPush(master, 900, 0, [](fiddle::BusPrintCapture *capture) {
      capture->targetFor("a")->clear();
    });
    printer.stop();
    std::unique_ptr<juce::AudioFormatReader> emptyReader(formats.createReaderFor(empty));
    CHECK(emptyReader && emptyReader->lengthInSamples == 0);
    CHECK(printer.startStems(directory, 8000, 80, {}, false, 0).isNotEmpty());
    CHECK(printer.startStems(directory, 8000, 80, {{"a", "A"}, {"a", "B"}}, false, 0).isNotEmpty());
  }

  {
    // Exercise the full channel count and a short final render block, not just
    // the stereo/two-stem cases (JUCE has distinct large-channel paths).
    fiddle::RealtimeMixPrinter printer;
    std::vector<fiddle::RealtimeMixPrinter::Stem> buses;
    for (int i = 0; i < 64; ++i) buses.push_back({juce::String(i), "Bus"});
    CHECK(printer.startStems(directory, 8000, 16, buses, true, 0).isNotEmpty());
    CHECK(printer.startStems(directory, 8000, 16, buses, false, 0).isEmpty());
    CHECK(printer.beginAt(1000));
    juce::AudioBuffer<float> master(2, 9);
    printer.renderAndPush(master, 1000, 0, [&](fiddle::BusPrintCapture *capture) {
      for (int i = 0; i < 64; ++i) {
        auto *bus = capture->targetFor(juce::String(i));
        for (int ch = 0; ch < 2; ++ch)
          juce::FloatVectorOperations::fill(bus->getWritePointer(ch), float(i + 1) / 100.0f, 9);
      }
    });
    printer.stop();
    CHECK(!static_cast<bool>(printer.state()["incomplete"]));
    const auto paths = printer.state()["files"];
    for (int i = 0; i < 64; ++i) {
      std::unique_ptr<juce::AudioFormatReader> stem(formats.createReaderFor(juce::File(paths[i].toString())));
      CHECK(stem && stem->lengthInSamples == 9);
      if (stem) {
        juce::AudioBuffer<float> samples(2, 9);
        CHECK(stem->read(&samples, 0, 9, 0, true, true));
        CHECK(std::abs(samples.getSample(1, 8) - float(i + 1) / 100.0f) < 1.e-6f);
      }
    }
  }
  {
    // Waking from idle may skip pre-roll frames. None belong to the take.
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.start(directory.getChildFile("pre-roll.wav"), 8000, 80).isEmpty());
    CHECK(printer.beginAt(1001));
    CHECK(printer.endAt(1017));
    juce::AudioBuffer<float> block(2, 80); block.clear();
    CHECK(printer.push(block, 900, 0));
    CHECK(printer.push(block, 970, 560));
    CHECK(printer.push(block, 1000, 800));
    CHECK(printer.push(block, 1010, 880));
    printer.stop();
    CHECK(!static_cast<bool>(printer.state()["incomplete"]));
    CHECK(static_cast<juce::int64>(printer.state()["samples"]) == 128);
  }
  // Even seemingly perfect timestamps must not mask a missing/repeated frame.
  // Starting at frame zero must not disable the continuity check.
  for (uint64_t nextFrame : {0, 79, 81, 160}) {
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.start(directory.getChildFile("frame-gap.wav"), 8000, 80).isEmpty());
    CHECK(printer.beginAt(1000));
    juce::AudioBuffer<float> block(2, 80); block.clear();
    CHECK(printer.push(block, 1000, 0));
    CHECK(!printer.push(block, 1010, nextFrame));
    CHECK(static_cast<bool>(printer.state()["incomplete"]));
    CHECK(printer.needsFinalizing(1010));
    printer.stop();
    CHECK(static_cast<juce::int64>(printer.state()["samples"]) == 80);
  }
  for (bool includeMaster : {false, true}) {
    // Transport Stop is not the end of the render: delayed/reverb output must
    // reach every file, with one shared, sample-accurate tail boundary.
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.startStems(directory, 8000, 80,
        {{"a", "Strings"}, {"b", "Reverb"}}, includeMaster, 0, 0.025).isEmpty());
    CHECK(printer.beginAt(1000));
    CHECK(!static_cast<bool>(printer.state()["capturingTail"]));
    CHECK(printer.endAt(1007));
    CHECK(!printer.endAt(1010)); // Duplicate Stop must not extend the tail.
    CHECK(static_cast<bool>(printer.state()["capturingTail"]));
    CHECK(!printer.beginAt(1010)); // Do not reset an unfinished take.
    juce::AudioBuffer<float> master(2, 80);
    for (int step = 0; step < 4; ++step) {
      CHECK(printer.isRecording());
      CHECK(!printer.needsFinalizing(1000 + 10 * step));
      printer.renderAndPush(master, 1000 + step * 10, uint64_t(step * 80), [&](fiddle::BusPrintCapture *capture) {
        CHECK(capture != nullptr);
        if (!capture) return;
        auto *a = capture->targetFor("a"), *b = capture->targetFor("b");
        for (int ch = 0; ch < 2; ++ch)
          for (int i = 0; i < 80; ++i) {
            const auto value = float(step * 80 + i) / 1000.0f;
            a->setSample(ch, i, value);
            b->setSample(ch, i, -value);
            master.setSample(ch, i, value * 0.5f);
          }
      });
    }
    CHECK(printer.needsFinalizing(1032));
    CHECK(!printer.isRecording());
    CHECK(!static_cast<bool>(printer.state()["capturingTail"]));
    printer.stop();
    CHECK(!static_cast<bool>(printer.state()["incomplete"]));
    const auto files = printer.state()["files"];
    CHECK(files.size() == (includeMaster ? 3 : 2));
    for (int index = 0; index < files.size(); ++index) {
      std::unique_ptr<juce::AudioFormatReader> stem(formats.createReaderFor(juce::File(files[index].toString())));
      CHECK(stem && stem->lengthInSamples == 256);
      if (!stem || stem->lengthInSamples != 256) continue;
      juce::AudioBuffer<float> audio(2, 256);
      CHECK(stem->read(&audio, 0, 256, 0, true, true));
      const float gain = includeMaster && index == 0 ? 0.5f : index == files.size() - 1 ? -1.0f : 1.0f;
      for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 256; ++i)
          CHECK(std::abs(audio.getSample(ch, i) - gain * i / 1000.0f) < 1.e-6f);
    }
  }
  {
    fiddle::RealtimeMixPrinter printer;
    const auto tailFile = directory.getChildFile("master-tail.wav");
    CHECK(printer.start(tailFile, 8000, 80, -1).isNotEmpty());
    CHECK(printer.start(tailFile, 8000, 80, 61).isNotEmpty());
    CHECK(printer.start(tailFile, 8000, 80, std::numeric_limits<double>::quiet_NaN()).isNotEmpty());
    CHECK(printer.start(tailFile, 8000, 80, 5).isEmpty());
    CHECK(printer.beginAt(1000));
    CHECK(printer.endAt(1001));
    CHECK(!printer.needsFinalizing(1252)); // Old stop+250ms fallback must not cut the tail.
    juce::AudioBuffer<float> block(2, 80); block.clear();
    CHECK(printer.push(block, 1000, 0));
    CHECK(printer.isRecording());
    printer.stop(); // Manual Stop & finish does not wait five seconds.
    CHECK(!printer.isRecording());
    CHECK(!static_cast<bool>(printer.state()["capturingTail"]));
    CHECK(!static_cast<bool>(printer.state()["incomplete"]));
    CHECK(static_cast<juce::int64>(printer.state()["samples"]) == 80);
  }
  for (int failure : {2, 3, 5}) {
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.startStems(directory, 8000, 80, {{"a", "A"}}, true, 0).isEmpty());
    CHECK(printer.beginAt(1000));
    juce::AudioBuffer<float> master(2, 80);
    const auto render = [&](fiddle::BusPrintCapture *capture) {
      master.clear();
      if (failure != 3) capture->targetFor("a")->clear();
      capture->latencyChanged = failure == 2;
    };
    printer.renderAndPush(master, 1000, 0, render);
    if (failure == 5) printer.renderAndPush(master, 1020, 160, render);
    CHECK(static_cast<bool>(printer.state()["incomplete"]));
    CHECK(printer.needsFinalizing(1020));
    printer.stop();
  }
  {
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.start(directory.getChildFile("overflow.wav"), 8000, 80).isEmpty());
    CHECK(printer.beginAt(1000));
    juce::AudioBuffer<float> oversized(2, 64001); // Larger than the entire FIFO.
    oversized.clear();
    CHECK(!printer.push(oversized, 1000, 0));
    CHECK(static_cast<juce::int64>(printer.state()["droppedBlocks"]) == 1);
    CHECK(static_cast<bool>(printer.state()["incomplete"]));
    CHECK(printer.needsFinalizing(1001));
    printer.stop();
  }
  {
    fiddle::RealtimeMixPrinter printer;
    CHECK(printer.startStems(directory, 8000, 80, {{"a", "A"}}, true, 0).isEmpty());
    CHECK(printer.beginAt(1000));
    std::atomic<bool> inside{false}, release{false}, stopped{false};
    std::thread renderer([&] {
      juce::AudioBuffer<float> master(2, 80);
      printer.renderAndPush(master, 1000, 0, [&](fiddle::BusPrintCapture *capture) {
        inside.store(true);
        while (!release.load()) std::this_thread::yield();
        capture->targetFor("a")->clear();
        master.clear();
      });
    });
    while (!inside.load()) std::this_thread::yield();
    std::thread stopper([&] { printer.stop(); stopped.store(true); });
    juce::Thread::sleep(10);
    CHECK(!stopped.load()); // UI stop cannot free a capture in use by rendering.
    release.store(true);
    renderer.join(); stopper.join();
    CHECK(stopped.load());
  }
  CHECK(directory.deleteRecursively());
  if (failures == 0)
    std::cout << "Real-time Master printing and WAV finalization passed\n";
  return failures == 0 ? 0 : 1;
}
