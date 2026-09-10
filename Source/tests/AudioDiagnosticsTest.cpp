#include "AudioDiagnostics.h"
#include "NativePlugin/AudioConsumer.h"
#include <cmath>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

static void renderTiming() {
  fiddle::AudioRenderDiagnostics d;
  d.prepare(48000);
  d.record(0, 1, 480); // 10% of a 10 ms budget
  d.record(10, 25, 480); // 150%, one overrun
  d.record(35, 37, 240, true); // long gap, 40%, ring overflow
  d.record(40, 41, 0); // zero block ignored
  d.publish();
  fiddle::AudioRenderDiagnostics::Snapshot s;
  CHECK(d.takeLatest(s));
  CHECK(s.callbacks == 3 && s.overruns == 1 && s.longGaps == 1);
  CHECK(std::abs(s.load - 18.0 / 25.0) < 1e-9);
  CHECK(s.peakLoad == 1.5 && s.maxLoad == 1.5 && s.maxRenderMs == 15);
  CHECK(s.minBlockSize == 240 && s.maxBlockSize == 480 && s.ringOverflows == 1);
  CHECK(s.lastOverrunMs == 25 && s.maxGapMs == 25);
  CHECK(!d.takeLatest(s));
  d.prepare(44100); // a restart must not count the stopped interval as a gap
  d.record(5000, 5001, 441, false, true);
  d.publish();
  CHECK(d.takeLatest(s) && s.longGaps == 1 && s.unavailableBlocks == 1);
}

static void saturatedAndConcurrent() {
  fiddle::AudioRenderDiagnostics d;
  d.prepare(1000);
  for (int i = 0; i < 300; ++i) d.record(i * 250.0, i * 250.0 + 1, 250);
  fiddle::AudioRenderDiagnostics::Snapshot s;
  CHECK(d.takeLatest(s));
  d.record(75000, 75001, 250);
  CHECK(d.takeLatest(s) && s.droppedReports == 44 && s.callbacks == 301);

  std::atomic<bool> done{false};
  std::thread producer([&] {
    for (int i = 301; i < 10000; ++i) d.record(i * 250.0, i * 250.0 + 1, 250);
    done.store(true, std::memory_order_release);
  });
  uint64_t previous = s.callbacks;
  while (!done.load(std::memory_order_acquire)) {
    if (d.takeLatest(s)) { CHECK(s.callbacks >= previous); previous = s.callbacks; }
  }
  producer.join();
}

static void pluginTiming() {
  fiddle::PluginRenderDiagnostics d;
  d.prepare(48000);
  d.record(1, 3, 480);
  d.record(11, 15, 480);
  d.record(20, 99, 0); // ignored
  d.publish();
  fiddle::PluginRenderDiagnostics::Snapshot s;
  CHECK(d.takeLatest(s));
  CHECK(s.calls == 2 && s.averageMs == 3 && s.peakMs == 4 && s.maxMs == 4);
  CHECK(std::abs(s.load - 0.3) < 1e-9 && s.blockSize == 480 && s.sampleRate == 48000);
  CHECK(!d.takeLatest(s));
  d.prepare(44100);
  d.record(100, 101, 1024);
  d.publish();
  CHECK(d.takeLatest(s) && s.blockSize == 1024 && s.sampleRate == 44100);
  CHECK(s.averageMs == 1 && s.peakMs == 1 && s.maxMs == 4 && s.calls == 3);
  for (int i = 0; i < 40; ++i) { d.record(200 + i, 201 + i, 1024); d.publish(); }
  CHECK(d.takeLatest(s));
  d.record(1000, 1001, 1024); d.publish();
  CHECK(d.takeLatest(s) && s.droppedReports == 8);

  fiddle::PluginRenderDiagnostics::beginBlock();
  fiddle::PluginRenderDiagnostics::addBlockWork(2);
  fiddle::PluginRenderDiagnostics::addBlockWork(3);
  CHECK(fiddle::PluginRenderDiagnostics::blockWorkMs() == 5);
  std::thread other([] {
    fiddle::PluginRenderDiagnostics::beginBlock();
    fiddle::PluginRenderDiagnostics::addBlockWork(99);
  });
  other.join();
  CHECK(fiddle::PluginRenderDiagnostics::blockWorkMs() == 5);

  fiddle::AudioRenderDiagnostics total;
  total.prepare(48000);
  total.record(0, 6, 480, false, false, 5);
  total.record(20, 24, 480, false, false, 3);
  total.publish();
  fiddle::AudioRenderDiagnostics::Snapshot all;
  CHECK(total.takeLatest(all));
  CHECK(std::abs(all.pluginLoad - 0.4) < 1e-9);
  CHECK(std::abs(all.otherLoad - 0.1) < 1e-9);
  CHECK(all.longGaps == 1 && all.lastLongGapMs == 20 && all.renderBeforeLongGapMs == 6);
  CHECK(all.recentMaxGapMs == 20);
}

static void consumerCounts() {
  // Isolated mmap: never opens the user's live audio file.
  char path[] = "/tmp/fiddle-diagnostics-test.XXXXXX";
  const int fd = mkstemp(path);
  CHECK(fd >= 0);
  using State = fiddle::AudioConsumer::SharedState;
  CHECK(ftruncate(fd, sizeof(State)) == 0);
  void *memory = mmap(nullptr, sizeof(State), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  CHECK(memory != MAP_FAILED);
  auto *state = new (memory) State{};
  state->sampleRate = 1000;
  state->magic.store(fiddle::AudioConsumer::kMagic);
  {
    fiddle::AudioConsumer consumer(path);
    float left[16]{}, right[16]{};
    float *channels[]{left, right};
    consumer.pullAudio(channels, 2, 0);
    CHECK(consumer.diagnostics().callbacks == 0);
    consumer.pullAudio(channels, 2, 16); // inactive stream, not an underrun
    CHECK(consumer.diagnostics().underruns == 0);
    state->active.store(1);
    for (int i = 0; i < 256; ++i) state->samples[i] = 0.25f;
    state->writeFrame.store(48);
    for (int i = 0; i < 3; ++i) consumer.pullAudio(channels, 2, 16);
    CHECK(left[0] == 0.25f && state->readFrame.load() == 48);
    consumer.pullAudio(channels, 2, 16); // one underrun episode
    CHECK(left[0] == 0);
    consumer.pullAudio(channels, 2, 16); // recovery, not a second underrun
    auto s = consumer.diagnostics();
    CHECK(s.callbacks == 6 && s.underruns == 1 && s.bufferingFrames == 32);
    CHECK(state->readFrame.load() == 80); // silence still consumes timeline
    state->writeFrame.store(128);
    for (int i = 0; i < 3; ++i) consumer.pullAudio(channels, 2, 16);
    consumer.pullAudio(channels, 2, 16);
    CHECK(consumer.diagnostics().underruns == 2);
    state->magic.store(0);
    consumer.pullAudio(channels, 2, 16);
    CHECK(consumer.diagnostics().unavailableFrames == 32);
  }
  munmap(memory, sizeof(State));
  close(fd);
  unlink(path);
}

int main() {
  try {
    renderTiming(); saturatedAndConcurrent(); pluginTiming(); consumerCounts();
    std::cout << "Audio diagnostics: timing, bounded handoff and real consumer counters passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
